# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-42: Rack's own drawing of its ui widgets, read back from a framebuffer (widgets-10 review gaps)."""

from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.errors import OwnershipError
from rackpy.raw import glfw, nanovg, ui
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import Svg
from rackpy.types import ValueQuantity

SIZE = Vec(60, 20)
DISC = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="30" height="30">'
    '<circle cx="15" cy="15" r="14" fill="#3a6ea5"/></svg>'
)  # original test artwork


def disc() -> Svg:
    loaded = Svg()
    loaded.load_string(DISC)
    return loaded


def near(found: tuple[int, int, int, int], expected: tuple[int, int, int, int]) -> bool:
    return all(abs(a - b) <= 2 for a, b in zip(found, expected, strict=True))


def dirty_of(framebuffer: Any) -> bool:
    return bool(framebuffer.dirty)  # a function: mypy would narrow an attribute it saw asserted


def rendered(child: w.Widget, size: Vec = SIZE) -> bytes:
    """The RGBA pixels Rack's draw() gives one widget in its own framebuffer."""
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), size)
    child.box = Rect(Vec(), size)
    frame.add_child(child)
    try:
        frame.render()
        _, _, pixels = HOOKS.framebuffer_pixels(frame)
        return bytes(pixels)
    finally:
        frame.remove_child(child)
        frame.close()


def drawn(pixels: bytes) -> bool:
    return any(pixels[3::4])  # some pixel is not transparent


def pixel(pixels: bytes, x: int, y: int, width: int = 60) -> tuple[int, int, int, int]:
    at = (y * width + x) * 4
    return (pixels[at], pixels[at + 1], pixels[at + 2], pixels[at + 3])


def test_rack_draws_each_ui_widget_into_pixels() -> None:
    quantity = ValueQuantity(0, 10, 4, "Level", " V")
    slider, bar = ui.Slider(), ui.ProgressBar()
    slider.quantity = bar.quantity = quantity
    texts: dict[str, Any] = {
        "Button": ui.Button(),
        "ChoiceButton": ui.ChoiceButton(),
        "RadioButton": ui.RadioButton(),
        "OptionButton": ui.OptionButton(),
        "MenuItem": ui.MenuItem(),
        "MenuLabel": ui.MenuLabel(),
        "ColorDotMenuItem": ui.ColorDotMenuItem(),
        "TextField": ui.TextField(),
        "Tooltip": ui.Tooltip(),
        "Label": ui.Label(),
    }
    for name, widget in texts.items():
        widget.text = name
    texts["ColorDotMenuItem"].color = nanovg.NVGcolor(1, 0, 0, 1)
    texts["Slider"], texts["ProgressBar"], texts["MenuSeparator"] = slider, bar, ui.MenuSeparator()
    nothing = rendered(w.Widget())
    assert not drawn(nothing)  # the control: an empty frame has no pixels
    try:
        for name, widget in texts.items():
            assert drawn(rendered(widget)), f"Rack drew no pixels for ui.{name}"
    finally:
        for widget in texts.values():
            widget.close()


def test_menu_and_scroll_widget_draw_their_owned_entries_and_scrollbars() -> None:
    menu, entry = ui.Menu(), ui.MenuItem()
    scroll = ui.ScrollWidget()
    menu.box = Rect(Vec(), Vec(100, 40))
    entry.box = Rect(Vec(), Vec(80, 20))
    entry.text = "Menu pixel"
    menu.add_child(entry)
    scroll.box = Rect(Vec(), Vec(50, 40))
    content = w.Widget()
    content.box = Rect(Vec(), Vec(200, 300))
    scroll.container.add_child(content)
    try:
        assert drawn(rendered(menu, Vec(100, 40)))  # Menu::draw delegates to its Rack-owned entry
        scroll.step()
        assert drawn(rendered(scroll, Vec(50, 40)))  # its valid-parent Scrollbar::draw paints the handle
    finally:
        menu.close()
        scroll.close()


def test_the_colour_dot_and_the_overlay_background_are_what_racks_draw_calls_paint() -> None:
    dot, overlay = ui.ColorDotMenuItem(), ui.MenuOverlay()
    try:
        dot.color = nanovg.NVGcolor(1, 0, 0, 1)
        dots = rendered(dot, Vec(60, 20))
        assert pixel(dots, 14, 10) == (255, 0, 0, 255)  # ColorDotMenuItem::draw: a circle of radius 6 at (14, h / 2)
        assert pixel(dots, 58, 2)[3] == 0 or pixel(dots, 58, 2) != (255, 0, 0, 255)  # and nowhere near the far corner
        assert not drawn(rendered(overlay))  # MenuOverlay's default background is transparent: nothing is drawn
        overlay.bg_color = nanovg.NVGcolor(0, 0, 1, 1)
        assert pixel(rendered(overlay), 30, 10) == (0, 0, 255, 255)  # MenuOverlay::draw fills its box with bgColor
    finally:
        dot.close()
        overlay.close()


def test_set_theme_changes_what_racks_widgets_draw_and_refresh_theme_restores_it() -> None:
    button = ui.Button()
    button.text = "Theme"
    try:
        before = rendered(button)
        ui.set_theme(nanovg.NVGcolor(1, 0, 0, 1), nanovg.NVGcolor(0, 1, 0, 1))
        try:
            themed = rendered(button)
            assert themed != before  # blendish was given other colours
        finally:
            ui.refresh_theme()  # back to the colours of Rack's own theme
        assert rendered(button) == before
    finally:
        button.close()


def test_slider_drags_lock_the_cursor_and_move_the_value_as_racks_handlers_do() -> None:
    # Slider::onDragStart locks the cursor for a left press only, onDragMove moves the scaled value by 0.001 per pixel,
    # onDragEnd unlocks it.
    settings = raw.settings.settings
    context = raw.context_get()
    assert context is not None and context.window is not None
    window = context.window
    allowed = settings.allow_cursor_lock
    settings.allow_cursor_lock = True
    quantity = ValueQuantity(0, 10, 0, "Level")
    slider = ui.Slider()
    try:
        slider.quantity = quantity
        slider.on_drag_start(w.DragStartEvent(button=glfw.GLFW_MOUSE_BUTTON_RIGHT, context=w.EventContext()))
        assert not window.is_cursor_locked()  # only the left button
        slider.on_drag_start(w.DragStartEvent(button=glfw.GLFW_MOUSE_BUTTON_LEFT, context=w.EventContext()))
        assert window.is_cursor_locked()
        slider.on_drag_move(w.DragMoveEvent(mouse_delta=Vec(50, 0), context=w.EventContext()))
        assert quantity.get_value() == pytest.approx(0.5)  # 50 px * 0.001 of the 0..10 range
        slider.on_drag_end(w.DragEndEvent(button=glfw.GLFW_MOUSE_BUTTON_LEFT, context=w.EventContext()))
        assert not window.is_cursor_locked()
    finally:
        window.cursor_unlock()
        settings.allow_cursor_lock = allowed
        slider.close()


def test_a_tooltip_is_sized_and_placed_inside_its_parent_by_step_and_needs_one() -> None:
    # Tooltip::step sizes the box from the text, puts it by the mouse and nudges it inside the parent (and asserts one).
    tooltip, holder = ui.Tooltip(), w.Widget()
    holder.box = Rect(Vec(), Vec(400, 300))
    try:
        tooltip.text = "Level"
        with pytest.raises(OwnershipError, match="parent"):
            tooltip.step()  # Rack's step asserts the parent
        holder.add_child(tooltip)
        tooltip.step()
        short = Vec(tooltip.box.size.x, tooltip.box.size.y)  # a copy: box.size is a live view of the field
        assert short.x > 0 and short.y > 0
        pos = tooltip.box.pos
        assert 0 <= pos.x <= 400 - short.x and 0 <= pos.y <= 300 - short.y
        tooltip.text = "a much, much longer line of tooltip text than the first one was"
        tooltip.step()
        assert tooltip.box.size.x > short.x  # the width follows the text
        pos = tooltip.box.pos
        assert 0 <= pos.x <= 400 - tooltip.box.size.x  # still nudged inside the parent
    finally:
        holder.close()


def test_text_field_copy_alone_fills_the_clipboard_and_a_left_press_places_the_cursor() -> None:
    class Field(ui.TextField):
        def get_text_position(self, mouse_pos: Vec) -> int:
            return 3

    field = Field()
    try:
        field.set_text("copy me")
        field.select_all()
        field.copy_clipboard()
        field.set_text("")
        field.paste_clipboard()
        assert field.get_text() == "copy me"  # copied without cutting
        field.cursor, field.selection = 0, 7
        press = w.ButtonEvent(pos=Vec(5, 5), button=glfw.GLFW_MOUSE_BUTTON_LEFT, action=glfw.GLFW_PRESS)
        press.context = w.EventContext()
        field.on_button(press)  # Rack: a left press sets cursor and selection to the position under the mouse
        assert (field.cursor, field.selection) == (3, 3)
        assert not ui.TextField().password and ui.PasswordField().password
    finally:
        field.close()


def test_framebuffer_step_steps_its_children_and_an_opengl_widget_is_dirty_every_step() -> None:
    steps: list[str] = []

    class Child(w.Widget):
        def step(self) -> None:
            steps.append("child")
            super().step()

    frame, gl, child = w.FramebufferWidget(), w.OpenGlWidget(), Child()
    try:
        frame.add_child(child)
        frame.step()
        assert steps == ["child"]
        gl.set_dirty(False)
        assert not dirty_of(gl)
        gl.step()
        assert dirty_of(gl)  # OpenGlWidget::step: dirty = true, every frame
    finally:
        frame.close()
        gl.close()


def test_opengl_widget_draws_racks_framebuffer_triangle() -> None:
    gl = w.OpenGlWidget()
    gl.box = Rect(Vec(), Vec(30, 30))
    try:
        gl.render()  # renders OpenGlWidget's own framebuffer; nesting it bypasses that in Rack
        _, _, pixels = HOOKS.framebuffer_pixels(gl)
        pixels = bytes(pixels)
        assert pixel(pixels, 5, 5, 30)[3] == 255  # Rack clears the framebuffer to opaque black
        assert any(pixels[i : i + 3] != b"\0\0\0" for i in range(0, len(pixels), 4))  # plus its RGB triangle
    finally:
        gl.close()


def test_transform_widget_draw_layer_transforms_its_child() -> None:
    transform = w.TransformWidget()
    transform.translate(Vec(30, 0))

    class Layer(w.Widget):
        def draw_layer(self, args: Any, layer: int) -> None:
            assert layer == 1
            nanovg.begin_path(args.vg)
            nanovg.rect(args.vg, 0, 0, 8, 8)
            nanovg.fill_color(args.vg, nanovg.rgb(255, 0, 0))
            nanovg.fill(args.vg)

    child = Layer()
    transform.add_child(child)

    class Root(w.Widget):
        def draw(self, args: Any) -> None:
            self.draw_child(transform, args, 1)

    root = Root()
    root.add_child(transform)
    try:
        pixels = rendered(root, Vec(60, 20))
        assert pixel(pixels, 4, 4)[3] == 0  # the untransformed location stays clear
        assert pixel(pixels, 34, 4) == (255, 0, 0, 255)
    finally:
        root.close()


def test_rack_draws_an_svg_widget_and_places_a_transform_widgets_child() -> None:
    sw, moved, inside = w.SvgWidget(), w.TransformWidget(), w.SvgWidget()
    try:
        sw.set_svg(disc())
        sw.wrap()
        plain = rendered(sw, Vec(30, 30))
        assert near(pixel(plain, 15, 15, 30), (0x3A, 0x6E, 0xA5, 255))  # SvgWidget::draw: the disc
        assert pixel(plain, 0, 0, 30)[3] == 0  # and nothing outside it
        inside.set_svg(disc())
        inside.wrap()
        moved.add_child(inside)
        moved.translate(Vec(30, 0))  # TransformWidget::draw applies the matrix to its children
        both = rendered(moved, Vec(60, 30))
        assert pixel(both, 15, 15, 60)[3] == 0  # not where the child sits untransformed
        assert near(pixel(both, 45, 15, 60), (0x3A, 0x6E, 0xA5, 255))
    finally:
        sw.close()
        moved.close()
