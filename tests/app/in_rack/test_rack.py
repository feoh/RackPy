# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28D: Rack's scene, rack and rack scroll widget as host views, and the LED displays, in Rack's live UI.

RP-28F: no plugin can subclass these or make a usable plug of its own, so their
host views are final and their UI virtuals are calls on Rack's own instances.

Rack's cables, and Python module widgets added to the rack, are tested in
windowed Rack (tests/app/test_module_browser.py), where a Python model's
widgets make real cables.
"""

import math
from collections.abc import Iterator
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import app, ui
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.engine import Module
from rackpy.raw.math import Rect, Vec

GLFW_RELEASE, GLFW_PRESS = 0, 1
GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_4 = 0, 3
GLFW_KEY_0, GLFW_KEY_LEFT, GLFW_KEY_PAGE_DOWN = 48, 263, 267
RACK_MOD_CTRL = 2  # GLFW_MOD_CONTROL on Linux


def scene() -> app.HostScene:
    context = raw.context_get()
    assert context is not None and context.scene is not None
    return context.scene


@pytest.fixture
def probe() -> Iterator[Any]:
    """The RuntimeProbe's module widget, which Rack made."""
    modules = scene().rack.get_modules()
    assert modules
    yield modules[0]
    scene().rack.deselect_all()


def test_the_scene_and_its_parts() -> None:
    s = scene()
    assert isinstance(s, app.HostScene) and s is scene()
    assert isinstance(s.rack, app.HostRackWidget) and isinstance(s.rack_scroll, app.HostRackScrollWidget)
    assert s.rack_scroll.rack_widget is s.rack and isinstance(s.menu_bar, w.HostWidget)
    assert isinstance(s.browser, w.HostWidget) and s.get_mouse_pos().is_finite()
    before = s.mouse_pos
    s.mouse_pos = Vec(1, 2)  # Rack's deprecated raw field, which Rack sets as the mouse moves
    assert s.mouse_pos == Vec(1, 2)
    s.mouse_pos = before
    with pytest.raises(ValueError):
        s.mouse_pos = Vec(math.nan, 0)


def test_the_rack_scroll_widget() -> None:
    scroll = scene().rack_scroll
    zoom, offset = scroll.get_zoom(), scroll.get_grid_offset()
    try:
        scroll.set_zoom(100)
        assert scroll.get_zoom() == 4  # Rack clamps zoom to 1/4 to 4
        scroll.set_zoom(0.5, Vec(10, 10))
        assert scroll.get_zoom() == 0.5
        scroll.set_grid_offset(Vec(2, 1))
        assert scroll.get_grid_offset().x == pytest.approx(2, abs=1e-3)
        with pytest.raises(ValueError):
            scroll.set_zoom(math.inf)
        with pytest.raises(ValueError):
            scroll.zoom_to_bound(Rect(Vec(), Vec(0, 10)))  # Rack divides by the size
        scroll.zoom_to_bound(Rect(Vec(0, 0), Vec(300, 200)))
        scroll.zoom_to_modules()
        scroll.reset()
        assert isinstance(scroll.zoom_widget, w.HostWidget)
    finally:
        scroll.set_zoom(zoom)
        scroll.set_grid_offset(offset)


def test_modules_selection_and_positions(probe: Any) -> None:
    rack = scene().rack
    # Rack 2.6.6's hasModules() returns moduleContainer->children.empty(): True only for an empty rack.
    assert not rack.has_modules() and probe in rack.get_modules()
    pos = probe.get_box().pos
    rack.select(probe)
    assert rack.is_selected(probe) and rack.has_selection() and rack.get_selected() == [probe]
    data = rack.selection_to_json()
    assert [m["model"] for m in data["modules"]] == ["RuntimeProbe"]
    assert not rack.is_selection_bypassed()
    assert rack.request_selection_pos(Vec(600, 0))
    assert probe.get_box().pos == pos.plus(Vec(600, 0))
    rack.set_selection_pos_nearest(Vec(-600, 0))
    assert probe.get_box().pos == pos
    with pytest.raises(ValueError):
        rack.request_selection_pos(Vec(math.nan, 0))
    with pytest.raises(ValueError):
        rack.set_selection_pos_nearest(Vec(math.inf, 0))
    rack.deselect_all()
    assert not rack.has_selection()
    rack.select_all()
    assert probe in rack.get_selected()
    rack.deselect_all()
    rack.update_module_old_positions()
    far = pos.plus(Vec(600, 0))
    assert rack.request_module_pos(probe, far) and probe.get_box().pos == far
    rack.set_module_pos_force(probe, pos)
    assert probe.get_box().pos == pos
    rack.set_module_pos_nearest(probe, pos)
    with pytest.raises(ValueError):
        rack.request_module_pos(probe, Vec(math.nan, 0))
    with pytest.raises(OwnershipError, match="not in Rack's rack"):
        rack.select(app.ModuleWidget())
    merged = rack.merge_json({"modules": [{"id": data["modules"][0]["id"]}], "cables": []})
    assert len(merged["modules"][0]["pos"]) == 2  # Rack's grid position
    with pytest.raises(OwnershipError, match="already has its widget"):
        rack.from_json({"modules": [{"id": data["modules"][0]["id"]}], "cables": []})
    assert rack.get_module(data["modules"][0]["id"]) is probe


def test_containers_cables_colours_and_menus() -> None:
    rack = scene().rack
    for container in (rack.get_module_container(), rack.get_plug_container(), rack.get_cable_container()):
        assert isinstance(container, w.HostWidget)
    assert rack.get_cables() == [] and rack.get_complete_cables() == [] and rack.get_incomplete_cables() == []
    with pytest.warns(DeprecationWarning):
        assert rack.get_incomplete_cable() is None
    colour = rack.get_next_cable_color_id()
    rack.set_next_cable_color_id(colour + 1)
    assert rack.get_next_cable_color_id() == colour + 1
    assert isinstance(rack.get_next_cable_color(), nvg.NVGcolor)
    rack.set_next_cable_color_id(colour)
    assert rack.get_mouse_pos().is_finite()
    menu = ui.Menu()
    try:
        rack.append_selection_context_menu(menu)  # Rack's selection menu items
        assert len(menu.children) > 0
    finally:
        menu.close()


def configure(m: Module) -> None:
    m.config(1, 0, 0, 0)
    m.config_param(0, 0, 1, 0, "Touch")


def test_the_touched_param() -> None:
    rack = scene().rack
    unbound = app.Knob()
    m = Module.create(configure)
    HOOKS.engine_adopt(m)
    bound = raw.create_param(app.Knob, Vec(), m, 0)
    try:
        with pytest.raises(OwnershipError, match="module"):
            rack.set_touched_param(unbound)  # Core's MIDI-Map reads its module
        rack.set_touched_param(bound)
        assert rack.get_touched_param() is bound and rack.touched_param is bound
        rack.touched_param = None
        assert rack.get_touched_param() is None
        rack.touched_param = bound
    finally:
        for widget in (unbound, bound):
            widget.close()
        HOOKS.engine_delete(m, None)
    assert rack.get_touched_param() is None  # released when the knob died


def test_host_widgets_dispatch_and_draw() -> None:
    bar = scene().menu_bar
    bar.on_dirty(w.DirtyEvent())  # Rack's handler of a Rack-owned widget
    for rack_only in ("on_add", "on_remove", "on_context_create", "on_context_destroy"):
        assert not hasattr(bar, rack_only)  # these announce changes only Rack makes
    drawn: list[str] = []

    class Copy(w.Widget):
        def draw(self, args: Any) -> None:
            bar.draw(args)  # Rack's drawing of its menu bar, into this framebuffer
            bar.draw_layer(args, 1)
            drawn.append("bar")

    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), Vec(120, 20))
    copy = Copy()
    copy.box = frame.box.copy()
    frame.add_child(copy)
    try:
        frame.render()
        assert drawn == ["bar"]
        with pytest.raises(ValueError):
            bar.draw(w.DrawArgs())  # an owned DrawArgs has no live context
    finally:
        frame.close()


def test_racks_own_scene_rack_and_plugs_are_not_subclassable(probe: Any) -> None:
    """RP-28F decision: only Rack makes usable ones, so their UI virtuals are calls on Rack's own instances."""
    for name in ("Scene", "RackScrollWidget", "RackWidget", "PlugWidget"):
        assert not hasattr(app, name)  # no class to make one with
    for view in (app.HostScene, app.HostRackScrollWidget, app.HostRackWidget, app.HostPlugWidget):
        with pytest.raises(TypeError, match="No constructor defined"):
            view()
        with pytest.raises(TypeError, match="not an acceptable base type"):
            type("Override", (view,), {"step": lambda self: None})
    s = scene()
    scroll, rack = s.rack_scroll, s.rack
    zoom, offset, mouse = scroll.get_zoom(), scroll.get_grid_offset(), s.mouse_pos
    try:
        scene_calls(s)
        scroll_calls(scroll, rack)
        rack_calls(rack, probe)
        draw_calls(s, scroll, rack)
    finally:
        s.on_hover_key(w.HoverKeyEvent(key=GLFW_KEY_LEFT, action=GLFW_RELEASE))
        scroll.set_zoom(zoom)
        scroll.set_grid_offset(offset)
        s.mouse_pos = mouse
    cable = app.CableWidget()
    plug = cable.input_plug
    try:
        assert isinstance(plug, app.HostPlugWidget) and plug.get_cable() is cable
        plug.step()  # Rack's PlugWidget::step, on a plug Rack's CableWidget constructor made
    finally:
        cable.close()
    with pytest.raises(StaleHandleError):
        plug.step()


def scene_calls(s: app.HostScene) -> None:
    stepped: list[bool] = []
    dropped: list[list[str]] = []

    class Child(w.Widget):
        def step(self) -> None:
            stepped.append(True)
            super().step()

        def on_path_drop(self, event: Any) -> None:
            dropped.append(list(event.paths))
            event.consume(self)

    child = Child()
    child.box = Rect(Vec(), Vec(10, 10))
    child.attach_to_scene()  # on top of Rack's own children
    try:
        s.step()  # Rack's Scene::step steps its children
        drop = w.PathDropEvent(["/nonexistent/rp28f.txt"], pos=Vec(5, 5), context=w.EventContext())
        s.on_path_drop(drop)  # not a patch or selection, so Rack's Scene sends it to the widget under it
        assert stepped and dropped == [["/nonexistent/rp28f.txt"]] and drop.get_target() is child
    finally:
        child.remove_from_parent()
        child.close()
    s.on_hover(w.HoverEvent(pos=Vec(7, 300)))
    assert s.get_mouse_pos() == Vec(7, 300)  # Scene::onHover records the mouse
    s.on_drag_hover(w.DragHoverEvent(button=GLFW_MOUSE_BUTTON_LEFT, pos=Vec(9, 301)))
    assert s.get_mouse_pos() == Vec(9, 301)
    s.rack_scroll.set_zoom(0.5)
    key = w.HoverKeyEvent(key=GLFW_KEY_0, key_name="0", action=GLFW_PRESS, mods=RACK_MOD_CTRL, context=w.EventContext())
    s.on_hover_key(key)
    assert s.rack_scroll.get_zoom() == 1 and key.is_consumed()  # Rack's Ctrl+0 command
    s.rack_scroll.reset()
    for _ in range(2):
        s.step()  # Rack's scroll widget settles its offset at this zoom
    before = s.rack_scroll.get_grid_offset()
    s.on_hover_key(w.HoverKeyEvent(key=GLFW_KEY_LEFT, action=GLFW_PRESS))
    s.step()  # Rack's Scene::step scrolls the rack 32 pixels while an arrow key is held
    assert s.rack_scroll.get_grid_offset().x < before.x - 1


def scroll_calls(scroll: app.HostRackScrollWidget, rack: app.HostRackWidget) -> None:
    scroll.reset()
    scroll.step()
    before = scroll.get_grid_offset()
    scroll.on_hover_key(w.HoverKeyEvent(pos=Vec(10, 10), key=GLFW_KEY_PAGE_DOWN, action=GLFW_PRESS))
    assert scroll.get_grid_offset().y > before.y  # Rack's ScrollWidget pages down
    zoom, offset = scroll.get_zoom(), scroll.get_grid_offset()
    scroll.on_hover_scroll(w.HoverScrollEvent(pos=Vec(10, 10), scroll_delta=Vec(0, -50)))
    assert (scroll.get_zoom(), scroll.get_grid_offset()) != (zoom, offset)  # scrolls, or zooms (Rack's settings)
    scroll.set_zoom(1)
    scroll.on_button(w.ButtonEvent(pos=Vec(10, 10), button=GLFW_MOUSE_BUTTON_4, action=GLFW_PRESS))
    assert scroll.get_zoom() == pytest.approx(2**-0.5)  # Rack zooms out with the fourth mouse button
    rack.on_hover(w.HoverEvent(pos=Vec(-12345, -6789)))
    scroll.on_hover(w.HoverEvent(pos=Vec(10, 10)))
    assert rack.get_mouse_pos() != Vec(-12345, -6789)  # Rack's scroll widget passes the hover to its rack
    scroll.set_grid_offset(Vec(1e5, 1e5))
    scroll.step()  # Rack's RackScrollWidget::step keeps the offset in the rack's area
    assert scroll.get_grid_offset().x < 1e5


def rack_calls(rack: app.HostRackWidget, probe: Any) -> None:
    box = probe.get_box()
    start, end = box.pos - Vec(5, 5), box.pos + box.size + Vec(5, 5)
    rack.deselect_all()
    rack.on_hover(w.HoverEvent(pos=start))
    assert rack.get_mouse_pos() == start
    rack.on_drag_start(w.DragStartEvent(button=GLFW_MOUSE_BUTTON_LEFT))  # Rack starts its selection rectangle
    rack.on_drag_hover(w.DragHoverEvent(button=GLFW_MOUSE_BUTTON_LEFT, pos=end))
    assert rack.get_mouse_pos() == end and rack.is_selected(probe)
    rack.deselect_all()
    assert not rack.is_selected(probe)
    rack.update_selection_from_rect()
    assert rack.is_selected(probe)  # Rack reselects modules intersecting the active rectangle
    rack.on_drag_end(w.DragEndEvent(button=GLFW_MOUSE_BUTTON_LEFT))
    rack.on_drag_hover(w.DragHoverEvent(button=GLFW_MOUSE_BUTTON_LEFT, pos=start))
    assert rack.get_mouse_pos() == start and rack.is_selected(probe)  # the rectangle has ended
    rack.deselect_all()
    button = w.ButtonEvent(pos=start, button=GLFW_MOUSE_BUTTON_LEFT, action=GLFW_RELEASE, context=w.EventContext())
    rack.on_button(button)
    assert button.is_consumed() and button.get_target() is rack  # Rack's rack is opaque to left clicks
    key = w.HoverKeyEvent(pos=start, key=GLFW_KEY_PAGE_DOWN, action=GLFW_RELEASE, context=w.EventContext())
    rack.on_hover_key(key)
    assert not key.is_propagating()  # RackWidget::onHoverKey (OpaqueWidget's) stops it
    rack.step()


def draw_calls(s: app.HostScene, scroll: app.HostRackScrollWidget, rack: app.HostRackWidget) -> None:
    drawn: list[str] = []

    class Copy(w.Widget):
        def draw(self, args: Any) -> None:
            for view in (s, scroll, rack):
                view.draw(args)  # Rack's drawing of its own widgets, into this framebuffer
            drawn.append("rack")

    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), Vec(64, 48))
    copy = Copy()
    copy.box = frame.box.copy()
    frame.add_child(copy)
    try:
        frame.render()
        assert drawn == ["rack"]
    finally:
        frame.close()


def test_led_displays() -> None:
    display, separator = app.LedDisplay(), app.LedDisplaySeparator()
    choice = app.LedDisplayChoice()
    field = app.LedDisplayTextField()
    try:
        choice.text, choice.font_path = "Gate", "res/fonts/ShareTechMono-Regular.ttf"
        choice.text_offset, choice.color, choice.bg_color = Vec(2, 3), nvg.rgb(255, 0, 0), nvg.rgb(0, 0, 0)
        assert (choice.text, choice.text_offset) == ("Gate", Vec(2, 3))
        field.text = "abc"
        field.text_offset, field.color, field.bg_color = Vec(1, 1), nvg.rgb(1, 2, 3), nvg.rgb(4, 5, 6)
        field.font_path = "res/fonts/ShareTechMono-Regular.ttf"
        field.box = Rect(Vec(), Vec(60, 20))
        assert isinstance(field, ui.TextField) and 0 <= field.get_text_position(Vec(30, 10)) <= 3
        display.box = Rect(Vec(), Vec(60, 40))
        for child in (separator, choice, field):
            child.box = Rect(Vec(), Vec(60, 20))
            display.add_child(child)
        frame = w.FramebufferWidget()
        frame.box = Rect(Vec(), Vec(60, 40))
        frame.add_child(display)
        frame.render()  # Rack draws them, text included
        frame.close()
    finally:
        for widget in (separator, choice, field, display):
            if widget.alive:
                widget.close()
