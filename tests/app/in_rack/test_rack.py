# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28D: Rack's scene, rack and rack scroll widget as host views, and the LED displays, in Rack's live UI.

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
from rackpy.errors import OwnershipError
from rackpy.raw import app, ui
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.engine import Module
from rackpy.raw.math import Rect, Vec


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
