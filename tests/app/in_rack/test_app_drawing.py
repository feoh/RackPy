# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-42: what Rack's own draw() of its app widgets paints, read back from a framebuffer (round-2 review gaps)."""

from collections.abc import Iterator
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.raw import app
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.engine import Module, ParamHandle
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import Svg

DISC = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="30" height="30">'
    '<circle cx="15" cy="15" r="14" fill="#3a6ea5"/></svg>'
)  # original test artwork


def disc() -> Svg:
    loaded = Svg()
    loaded.load_string(DISC)
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


def rendered(child: w.Widget, size: Vec) -> bytes:
    """The RGBA pixels Rack's draw() gives one widget in a framebuffer of that size, the widget at its corner."""
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), size)
    child.box = Rect(Vec(), child.box.size) if child.box.size.x < 1e9 else Rect(Vec(), size)
    frame.add_child(child)
    try:
        frame.render()
        _, _, pixels = HOOKS.framebuffer_pixels(frame)
        return bytes(pixels)
    finally:
        frame.remove_child(child)
        frame.close()


def pixel(pixels: bytes, x: int, y: int, width: int) -> tuple[int, int, int, int]:
    at = (y * width + x) * 4
    return (pixels[at], pixels[at + 1], pixels[at + 2], pixels[at + 3])


def test_circular_shadow_paints_a_black_disc_with_its_opacity_and_blur() -> None:
    # CircularShadow::draw: nothing at opacity 0; else a black circle (or a radial blur of it) of that opacity.
    shadow = app.CircularShadow()
    try:
        shadow.box = Rect(Vec(), Vec(30, 30))
        shadow.blur_radius, shadow.opacity = 0.0, 0.0
        assert not any(rendered(shadow, Vec(30, 30))[3::4])
        shadow.opacity = 0.8
        hard = rendered(shadow, Vec(30, 30))
        centre, corner = pixel(hard, 15, 15, 30), pixel(hard, 0, 0, 30)
        assert centre[:3] == (0, 0, 0) and abs(centre[3] - 204) <= 3 and corner[3] == 0
        shadow.blur_radius = 6.0
        soft = rendered(shadow, Vec(30, 30))
        assert abs(pixel(soft, 15, 15, 30)[3] - 204) <= 3  # inside the blur: the full opacity
        assert 0 < pixel(soft, 15, 2, 30)[3] < 204  # on the radial falloff: some, not all
    finally:
        shadow.close()


def test_panel_border_strokes_the_edge_and_leaves_the_inside() -> None:
    # PanelBorder::draw: a 1 px stroke of grey at half opacity along the box's edge.
    border = app.PanelBorder()
    try:
        border.box = Rect(Vec(), Vec(30, 40))
        pixels = rendered(border, Vec(30, 40))
        edge, inside = pixel(pixels, 0, 20, 30), pixel(pixels, 15, 20, 30)
        assert edge[3] > 0 and edge[0] == edge[1] == edge[2] and inside[3] == 0
    finally:
        border.close()


def test_param_widget_marks_a_mapped_param_in_its_corner(module: Module) -> None:
    # ParamWidget::draw: a param with a handle (MIDI-Map's) gets a square of the handle's colour in its corner.
    knob = raw.create_param(app.Knob, Vec(), module, 0)
    handle = ParamHandle()
    context = raw.context_get()
    assert context is not None and context.engine is not None
    engine = context.engine
    try:
        knob.box = Rect(Vec(), Vec(30, 30))
        assert pixel(rendered(knob, Vec(30, 30)), 27, 27, 30)[3] == 0  # no handle: nothing is drawn
        handle.color = nvg.rgb(255, 0, 0)
        engine.add_param_handle(handle)
        engine.update_param_handle(handle, module.id, 0)
        mark = pixel(rendered(knob, Vec(30, 30)), 27, 27, 30)
        assert mark[0] > 200 and mark[1] < 40 and mark[3] == 255  # the handle's colour, in the corner
        assert pixel(rendered(knob, Vec(30, 30)), 5, 5, 30)[3] == 0  # and only there
    finally:
        engine.remove_param_handle(handle)
        knob.close()


def test_port_and_module_widgets_draw_their_children_and_a_bypassed_module_dimmer(module: Module) -> None:
    # PortWidget::draw and ModuleWidget::draw are Widget::draw (the latter at a third of the alpha when bypassed).
    port = raw.create_input(app.SvgPort, Vec(), module, 0)
    plain = raw.create_input(app.PortWidget, Vec(), module, 0)
    try:
        port.sw.set_svg(disc())
        port.box = Rect(Vec(), Vec(30, 30))
        plain.box = Rect(Vec(), Vec(30, 30))
        shown = rendered(port, Vec(30, 30))
        assert abs(pixel(shown, 15, 15, 30)[2] - 0xA5) <= 2 and not any(rendered(plain, Vec(30, 30))[3::4])
    finally:
        port.close()
        plain.close()

    class Body(app.ModuleWidget):
        def __init__(self) -> None:
            super().__init__()
            self.draws: list[int] = []

        def draw(self, args: Any) -> None:
            self.draws.append(1)
            super().draw(args)

        def draw_layer(self, args: Any, layer: int) -> None:
            self.draws.append(layer)
            super().draw_layer(args, layer)

    widget = Body()
    holder = w.SvgWidget()
    layer_frame: w.FramebufferWidget | None = None
    layer_room: w.Widget | None = None
    try:
        holder.set_svg(disc())
        holder.wrap()
        widget.box = Rect(Vec(), Vec(30, 30))
        widget.add_child(holder)
        opaque = pixel(rendered(widget, Vec(30, 30)), 15, 15, 30)
        assert widget.draws and opaque[3] == 255

        class LayerRoom(w.Widget):
            def draw(self, args: Any) -> None:
                for child in self.children:
                    self.draw_child(child, args, 1)

        layer_room = LayerRoom()
        layer_room.box = Rect(Vec(), Vec(30, 30))
        layer_room.add_child(widget)
        layer_frame = w.FramebufferWidget()
        layer_frame.box = Rect(Vec(), Vec(30, 30))
        layer_frame.add_child(layer_room)
        layer_frame.render()
        assert 1 in widget.draws  # the parent dispatched Rack's module drawLayer on layer 1
    finally:
        if layer_frame is not None and layer_frame.alive:
            if layer_room is not None:
                layer_frame.remove_child(layer_room)
            layer_frame.close()
        if layer_room is not None and layer_room.alive:
            layer_room.remove_child(widget)
            layer_room.close()
        widget.close()
