# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28A: Rack's controls, ports, lights and SVG widgets as Python classes, in Rack's live UI.

Modules are RackPy modules handed to Rack's engine (the engine_adopt hook);
input goes through Rack's EventState, as the window would send it.
"""

import gc
import math
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import app, ui
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.engine import Module, Port
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import Svg

LEFT, PRESS, RELEASE = 0, 1, 0
# Original test artwork.
KNOB_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="30" height="30">'
    '<circle cx="15" cy="15" r="14" fill="#3a6ea5"/></svg>'
)
FRAME_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="12" height="24"><rect width="12" height="24" fill="#c04040"/></svg>'
)


def svg(text: str = KNOB_SVG) -> Svg:
    loaded = Svg()
    loaded.load_string(text)
    return loaded


def configure(m: Module) -> None:
    m.config(2, 1, 1, 2)
    m.config_param(0, 0, 10, 5, "Gain")
    m.config_param(1, 0, 1, 0, "Mode")
    m.config_input(0, "In")
    m.config_output(0, "Out")
    m.config_light(0, "Level")


@pytest.fixture
def module() -> Iterator[Module]:
    m = Module.create(configure)
    HOOKS.engine_adopt(m)  # Rack's engine owns it now
    yield m
    if m.alive:
        HOOKS.engine_delete(m, None)


def state() -> w.EventState:
    context = raw.context_get()
    assert context is not None and context.event is not None
    return context.event


def centre(widget: w.Widget) -> Vec:
    root = state().root_widget
    assert root is not None
    return widget.get_relative_offset(widget.box.size.div(2), root)


def alive(handle: Any) -> bool:
    return bool(handle.alive)


def module_of(control: Any) -> Module | None:
    module: Module | None = control.module  # read again: mypy would narrow after an assignment
    return module


def test_module_fields_take_only_engine_modules_with_valid_ids(module: Module) -> None:
    knob = app.Knob()
    try:
        assert knob.module is None and knob.param_id == -1
        with pytest.raises(ValueError, match="out of range"):
            knob.module = module  # param_id -1: Rack would index paramQuantities[-1]
        knob.param_id = 1
        knob.module = module
        assert knob.module is module and knob.get_param_quantity() is not None
        with pytest.raises(ValueError, match="out of range"):
            knob.param_id = 2
        assert knob.param_id == 1
        detached = Module.create(configure)
        with pytest.raises(OwnershipError, match="Rack's engine"):
            knob.module = detached  # Rack's history and cables find modules by ID
        detached.close()
        with pytest.raises(TypeError):
            knob.module = "module"  # type: ignore[assignment]
        knob.module = None
        assert knob.module is None and knob.get_param_quantity() is None
        knob.param_id = 7  # unchecked without a module, as in Rack
    finally:
        knob.close()

    def constructing(m: Module) -> None:
        configure(m)
        early = app.ParamWidget()
        early.param_id = 0
        with pytest.raises(OwnershipError, match="constructor"):
            early.module = m
        early.close()

    Module.create(constructing).close()


def test_knob_switch_and_slider_fields_and_param_quantity_setup(module: Module) -> None:
    knob, switch = app.Knob(), app.Switch()
    knob.speed, knob.snap, knob.smooth, knob.horizontal, knob.force_linear = 2.0, False, True, True, True
    knob.min_angle, knob.max_angle = -1.5, 1.5
    assert (knob.speed, knob.horizontal, knob.force_linear, knob.min_angle) == (2.0, True, True, -1.5)
    try:
        knob.param_id, knob.module = 0, module
        knob.init_param_quantity()  # Rack's Knob: smoothing on unless it snaps
        pq = module.param_quantities[0]
        assert pq.smooth_enabled and not pq.snap_enabled
        switch.momentary = True
        switch.param_id, switch.module = 1, module
        switch.init_param_quantity()  # Rack's Switch: snapping, no smoothing, momentary ones never reset
        mode = module.param_quantities[1]
        assert mode.snap_enabled and not mode.smooth_enabled and not mode.reset_enabled
        assert isinstance(app.SliderKnob(), app.Knob)
    finally:
        knob.close()
        switch.close()


def test_rack_turns_a_knob_and_flips_a_switch(module: Module) -> None:
    knob = raw.create_param(app.Knob, Vec(300, 300), module, 0)
    knob.box.size = Vec(30, 30)
    switch = raw.create_param_centered(app.Switch, Vec(400, 315), module, 1)
    switch.box = Rect(Vec(385, 300), Vec(30, 30))
    for widget in (knob, switch):
        widget.attach_to_scene()
    events = state()
    try:
        gain = module.param_quantities[0]
        start = gain.get_value()  # Rack's Knob smooths: it sets the engine's target, which this reads
        at = centre(knob)
        events.handle_button(at, LEFT, PRESS, 0)
        for step in range(1, 6):
            events.handle_hover(at.plus(Vec(0, -10 * step)), Vec(0, -10))  # drag up: Rack's Knob raises the value
        events.handle_button(at.plus(Vec(0, -50)), LEFT, RELEASE, 0)
        assert gain.get_value() > start
        assert HOOKS.touched_param() is knob  # Rack's MIDI-Map would learn this param
        flip = centre(switch)
        events.handle_button(flip, LEFT, PRESS, 0)
        events.handle_button(flip, LEFT, RELEASE, 0)
        assert module.params[1].value == 1  # Rack's Switch steps to its next value
        knob.reset_action()
        assert gain.get_value() == 5  # Rack's reset to the default
    finally:
        knob.remove_from_parent()
        switch.remove_from_parent()
        knob.close()
        switch.close()
    assert HOOKS.touched_param() is None  # released when the knob was deleted


def test_deleting_the_module_clears_controls(module: Module) -> None:
    knob = raw.create_param(app.Knob, Vec(300, 300), module, 0)
    knob.box.size = Vec(30, 30)
    port = raw.create_input(app.PortWidget, Vec(0, 0), module, 0)
    light = app.ModuleLightWidget()
    light.add_base_color(nvg.rgb(0, 255, 0))
    light.first_light_id = 0
    light.module = module
    knob.attach_to_scene()
    events = state()
    try:
        at = centre(knob)
        events.handle_button(at, LEFT, PRESS, 0)
        events.handle_button(at, LEFT, RELEASE, 0)
        assert HOOKS.touched_param() is knob
        HOOKS.engine_delete(module, None)  # Rack deletes the module
        gc.collect()
        assert all(module_of(control) is None for control in (knob, port, light))
        assert HOOKS.touched_param() is None  # MIDI-Map would have read the deleted module
        knob.step()
        port.step()
        light.step()  # Rack's browser-preview path: no module
        assert light.color.a == pytest.approx(1)
    finally:
        knob.remove_from_parent()
        for widget in (knob, port, light):
            widget.close()


def test_ports_check_ids_and_reach_the_module(module: Module) -> None:
    port = app.PortWidget()
    try:
        port.port_id = 0
        port.type = Port.Type.OUTPUT
        port.module = module
        assert port.get_port() is not None and port.get_port_info() is not None
        assert port.get_port_info().name == "Out"  # type: ignore[union-attr]
        with pytest.raises(ValueError, match="out of range"):
            port.port_id = 1
        with pytest.raises(TypeError):
            port.type = 5  # type: ignore[assignment]  # a Port.Type
        port.type = Port.Type.INPUT
        assert port.get_port_info().name == "In"  # type: ignore[union-attr]
        input_port = raw.create_input_centered(app.PortWidget, Vec(10, 10), module, 0)
        output_port = raw.create_output(app.PortWidget, Vec(10, 10), module, 0)
        assert input_port.type == Port.Type.INPUT and output_port.type == Port.Type.OUTPUT
        for p in (input_port, output_port):
            p.close()
    finally:
        port.close()


def test_ports_outside_their_module_widget_make_no_cables(module: Module) -> None:
    out = raw.create_output(app.PortWidget, Vec(300, 300), module, 0)
    out.box.size = Vec(24, 24)
    drags: list[str] = []

    class Watched(app.PortWidget):
        def on_drag_start(self, event: Any) -> None:
            drags.append("start")
            super().on_drag_start(event)  # Rack's behaviour, but no cable outside the module widget

    inp = raw.create_input(Watched, Vec(360, 300), module, 0)
    inp.box.size = Vec(24, 24)
    for p in (out, inp):
        p.attach_to_scene()
    events = state()
    before = HOOKS.rack_cables()
    try:
        events.handle_button(centre(inp), LEFT, PRESS, 0)
        events.handle_hover(centre(out), centre(out).minus(centre(inp)))
        events.handle_button(centre(out), LEFT, RELEASE, 0)
        assert drags == ["start"]  # the Python override ran
        assert HOOKS.rack_cables() == before  # Rack's cable behaviour did not
    finally:
        for p in (out, inp):
            p.remove_from_parent()
            p.close()


def test_module_lights_follow_the_engine(module: Module) -> None:
    light = raw.create_light_centered(app.ModuleLightWidget, Vec(10, 10), None, 0)
    try:
        light.base_colors = [nvg.rgb(255, 0, 0)]
        assert light.get_num_colors() == 1
        colors = light.base_colors
        assert isinstance(colors, list)
        colors.clear()
        assert light.get_num_colors() == 1
        light.module = module
        with pytest.raises(ValueError, match="out of range"):
            light.base_colors = [nvg.rgb(255, 0, 0)] * 3  # only two lights from 0
        light.add_base_color(nvg.rgb(0, 0, 255))
        with pytest.raises(ValueError, match="out of range"):
            light.add_base_color(nvg.rgb(0, 255, 0))
        with pytest.raises(ValueError, match="out of range"):
            light.first_light_id = 1  # lights 1 and 2
        module.lights[0].brightness = 1.0
        module.lights[1].brightness = 0.0
        light.step()  # Rack's ModuleLightWidget reads the module's lights
        assert light.color.rgba == pytest.approx((1, 0, 0, 1))
        module.lights[0].brightness = 0.25
        light.step()
        assert light.color.a == pytest.approx(0.5)  # Rack takes the square root for LEDs
        assert light.get_light(1) is not None and light.get_light_info() is not None
        with pytest.raises(IndexError):
            light.get_light(2)
        with pytest.raises(ValueError, match="one brightness per"):
            light.set_brightnesses([1.0])
        light.set_brightnesses([0.0, 1.0])
        assert light.color.rgba == pytest.approx((0, 0, 1, 1))
    finally:
        light.close()


def test_light_drawing_overrides_and_layers() -> None:
    calls: list[str] = []

    class Lamp(app.LightWidget):
        def draw_background(self, args: Any) -> None:
            calls.append("background")
            super().draw_background(args)

        def draw_light(self, args: Any) -> None:
            calls.append("light")
            nvg.begin_path(args.vg)
            nvg.rect(args.vg, 0, 0, 10, 10)
            nvg.fill_color(args.vg, nvg.rgb(255, 255, 0))
            nvg.fill(args.vg)

        def draw_halo(self, args: Any) -> None:
            calls.append("halo")

    class Room(w.Widget):
        def draw(self, args: Any) -> None:
            super().draw(args)  # layer 0: Rack's LightWidget::draw draws the background
            for child in self.children:
                self.draw_child(child, args, 1)  # layer 1: the light and its halo

    lamp = Lamp()
    lamp.box = Rect(Vec(), Vec(10, 10))
    lamp.color = nvg.rgb(255, 255, 0)
    lamp.bg_color, lamp.border_color = nvg.rgb(0, 0, 0), nvg.rgb(9, 9, 9)
    room = Room()
    room.box = Rect(Vec(), Vec(10, 10))
    room.add_child(lamp)
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), Vec(10, 10))
    frame.add_child(room)
    try:
        frame.render()
        assert calls == ["background", "light", "halo"]
        _, _, pixels = HOOKS.framebuffer_pixels(frame)
        assert tuple(pixels[(5 * 10 + 5) * 4 : (5 * 10 + 5) * 4 + 3]) == (255, 255, 0)
    finally:
        frame.close()


def test_svg_controls_and_their_host_children() -> None:
    knob = app.SvgKnob()
    try:
        knob.set_svg(svg())
        assert knob.box.size == Vec(30, 30)  # Rack sizes the knob to its SVG
        sw, fb, shadow, tw = knob.sw, knob.fb, knob.shadow, knob.tw
        assert isinstance(sw, w.HostSvgWidget) and sw.svg is not None and sw.svg.get_size() == Vec(30, 30)
        assert isinstance(fb, w.HostFramebufferWidget) and isinstance(tw, w.HostTransformWidget)
        assert isinstance(shadow, w.HostCircularShadow)
        shadow.opacity = 0.0
        assert shadow.opacity == 0.0 and shadow.blur_radius >= 0
        fb.set_dirty(False)
        fb.dirty = True
        assert bool(fb.dirty)
        with pytest.raises(ValueError, match="oversample"):
            fb.oversample = math.inf
        assert len(tw.transform) == 6
        tw.identity()
        assert tw.transform == (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
        with pytest.raises(ValueError, match="no image loaded"):
            knob.set_svg(Svg())  # Rack's svgDraw would crash on it
        with pytest.raises(ValueError, match="no image loaded"):
            sw.svg = Svg()
        with pytest.warns(DeprecationWarning, match="set_svg"):
            knob.set_svg_legacy(svg())
    finally:
        knob.close()
    assert not alive(sw) and not alive(fb)  # Rack deleted its own children with the knob
    with pytest.raises(StaleHandleError):
        _ = sw.svg


def test_switches_buttons_sliders_ports_screws_and_panels() -> None:
    switch, button, slider = app.SvgSwitch(), app.SvgButton(), app.SvgSlider()
    port, themed_port = app.SvgPort(), app.ThemedSvgPort()
    screw, themed_screw = app.SvgScrew(), app.ThemedSvgScrew()
    panel, themed_panel = app.SvgPanel(), app.ThemedSvgPanel()
    widgets: list[w.Widget] = [switch, button, slider, port, themed_port, screw, themed_screw, panel, themed_panel]
    light, dark = svg(), svg(FRAME_SVG)
    try:
        switch.add_frame(light)
        switch.frames = [light, dark]
        switch.latch = True
        assert switch.frames == [light, dark] and switch.latch
        with pytest.raises(ValueError):
            switch.frames = [Svg()]
        with pytest.raises(ValueError):
            button.add_frame(None)  # type: ignore[arg-type]
        button.add_frame(light)
        assert button.frames == [light] and button.box.size == Vec(30, 30)
        slider.set_background_svg(dark)
        slider.set_handle_svg(light)
        slider.set_handle_pos(Vec(0, 10), Vec(0, 0))
        assert slider.min_handle_pos == Vec(0, 10) and slider.max_handle_pos == Vec(0, 0)
        slider.set_handle_pos_centered(Vec(6, 20), Vec(6, 4))
        assert isinstance(slider.background, w.HostSvgWidget) and isinstance(slider.handle, w.HostSvgWidget)
        with pytest.warns(DeprecationWarning):
            slider.set_svgs(dark, light)
        port.set_svg(light)
        assert port.box.size == Vec(30, 30)
        themed_port.set_svg(light, dark)
        assert themed_port.light_svg is light and themed_port.dark_svg is dark
        assert themed_port.sw.svg is light  # Rack shows the light SVG unless dark panels are preferred
        screw.set_svg(light)
        themed_screw.set_svg(light, dark)
        assert themed_screw.sw.svg is light
        panel.set_background(dark)
        assert panel.svg is dark and isinstance(panel.panel_border, w.HostWidget)
        assert panel.box.size == Vec(15, 0) or panel.box.size.x > 0  # Rack rounds panels to the rack grid
        themed_panel.set_background(light, dark)
        assert themed_panel.light_svg is light and themed_panel.svg is light
        for cls in (app.RailWidget, app.PanelBorder, app.CircularShadow):
            cls().close()
        shadow = app.CircularShadow()
        shadow.blur_radius, shadow.opacity = 3.0, 0.5
        assert (shadow.blur_radius, shadow.opacity) == (3.0, 0.5)
        shadow.close()
    finally:
        for widget in widgets:
            widget.close()


def test_svg_loads_are_transactional() -> None:
    image = svg()
    with pytest.raises(raw.Exception):
        image.load_file("/nonexistent.svg")
    assert image.get_size() == Vec(30, 30)  # unlike Rack's, the previous image stays
    image.load_string("not an svg")  # NanoSVG parses anything, here into an empty image
    assert image.get_size() == Vec() and image.handle is not None


def test_widget_factories(tmp_path: Path) -> None:
    panel_svg = tmp_path / "panel.svg"
    panel_svg.write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="45" height="380">'
        '<rect width="45" height="380" fill="#202830"/></svg>'
    )
    made = raw.create_widget(w.Widget, Vec(3, 4))
    centred = raw.create_widget_centered(app.CircularShadow, Vec(20, 20))
    panel = raw.create_panel(str(panel_svg))
    themed = raw.create_panel(str(panel_svg), str(panel_svg))
    try:
        assert made.box.pos == Vec(3, 4)
        assert isinstance(panel, app.SvgPanel) and not isinstance(panel, app.ThemedSvgPanel)
        assert panel.box.size == Vec(45, 380) and isinstance(themed, app.ThemedSvgPanel)
        with pytest.raises(TypeError):
            raw.create_panel(str(panel_svg), str(panel_svg), panel_type=app.SvgPanel)
        with pytest.raises(TypeError):
            raw.create_param(w.Widget, Vec(), None, 0)  # type: ignore[type-var]
        preview = raw.create_param(app.Knob, Vec(1, 2), None, 0)  # a browser preview has no module
        assert preview.module is None and preview.param_id == 0 and preview.box.pos == Vec(1, 2)
        preview.close()
    finally:
        for widget in (made, centred, panel, themed):
            widget.close()


def test_constants_and_deprecated_aliases() -> None:
    assert app.RACK_GRID_WIDTH == 15 and app.RACK_GRID_HEIGHT == 380
    assert Vec(15, 380) == app.RACK_GRID_SIZE and Vec(30000, 38000) == app.RACK_OFFSET
    for old, new in (("SVGKnob", "SvgKnob"), ("SVGPanel", "SvgPanel"), ("SVGPort", "SvgPort")):
        with pytest.warns(DeprecationWarning, match=new):
            assert getattr(app, old) is getattr(app, new)


# Last: Rack's context menus stay open until the next frame closes them.
_kept: list[w.Widget] = []


def test_rack_asks_python_controls_for_context_menu_items(module: Module) -> None:
    added: list[Any] = []

    class Labelled(app.Knob):
        def append_context_menu(self, menu: Any) -> None:
            item = raw.create_menu_item("From Python", "", lambda: None)
            menu.add_child(item)  # Rack is building this menu, so Python may fill it
            added.append((menu, item))

    knob = raw.create_param(Labelled, Vec(300, 300), module, 0)
    knob.box.size = Vec(30, 30)
    knob.attach_to_scene()
    knob.create_context_menu()  # Rack's menu, then Rack calls append_context_menu
    menu, item = added[0]
    assert isinstance(item, ui.MenuItem) and item.parent is menu and item.alive
    knob.remove_from_parent()
    knob.close()  # closes its open menu too: Rack's menu items point at the knob
    assert not alive(item)
    port = raw.create_output(app.PortWidget, Vec(0, 0), module, 0)
    port.attach_to_scene()
    port.create_context_menu()
    _kept.append(port)  # its menu closes on the next frame; Rack deletes the port at exit
