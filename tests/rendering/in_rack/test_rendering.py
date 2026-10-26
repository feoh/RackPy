# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-27A: NanoVG drawing and rack::window resources, rendered by actual Rack.

Drawing runs in Python draw() callbacks that Rack's FramebufferWidget calls;
the pixels are read back from the framebuffer Rack rendered (premultiplied
RGBA, top row first).
"""

import gc
import math
import struct
import threading
import zlib
from collections.abc import Callable
from pathlib import Path
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

import rackpy._bootstrap
from rackpy import raw
from rackpy.errors import ContextError, OwnershipError, StaleHandleError
from rackpy.raw import asset
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw import window as win
from rackpy.raw.math import Rect, Vec

SIZE = 40
RED, GREEN, BLUE, WHITE = nvg.rgb(255, 0, 0), nvg.rgb(0, 255, 0), nvg.rgb(0, 0, 255), nvg.rgb(255, 255, 255)
FONT = "res/fonts/DejaVuSans.ttf"


class Canvas(w.Widget):
    """A widget whose draw() calls paint(vg, args)."""

    def __init__(self, paint: Callable[[w.GraphicsContext, Any], None]) -> None:
        super().__init__()
        self.paint = paint

    def draw(self, args: Any) -> None:
        self.paint(args.vg, args)


class Pixels:
    def __init__(self, width: int, height: int, data: bytes) -> None:
        self.width, self.height, self.data = width, height, data
        self.scale = width / SIZE  # Rack's pixel ratio

    def at(self, x: float, y: float) -> tuple[int, int, int, int]:
        """The premultiplied RGBA at widget coordinates (x, y)."""
        i = (int(y * self.scale) * self.width + int(x * self.scale)) * 4
        r, g, b, a = self.data[i : i + 4]
        return r, g, b, a

    def lit(self) -> int:
        return sum(1 for i in range(3, len(self.data), 4) if self.data[i] > 0)


def near(actual: tuple[int, ...], expected: tuple[int, ...], tolerance: int = 8) -> bool:
    return all(abs(a - e) <= tolerance for a, e in zip(actual, expected, strict=True))


def render(*widgets: w.Widget) -> Pixels:
    """Renders the widgets in a SIZE x SIZE FramebufferWidget, as Rack does, and reads it back."""
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), Vec(SIZE, SIZE))
    for widget in widgets:
        if not widget.box.size.is_finite():  # Rack's widgets start infinitely large
            widget.box = Rect(Vec(), Vec(SIZE, SIZE))
        frame.add_child(widget)
    try:
        frame.render()
        return Pixels(*HOOKS.framebuffer_pixels(frame))
    finally:
        frame.close()


def paint(fn: Callable[[w.GraphicsContext, Any], None]) -> Pixels:
    return render(Canvas(fn))


def fill_rect(vg: w.GraphicsContext, x: float, y: float, width: float, height: float, color: Any) -> None:
    nvg.begin_path(vg)
    nvg.rect(vg, x, y, width, height)
    nvg.fill_color(vg, color)
    nvg.fill(vg)


def write_png(path: Path, width: int, height: int, rgba: bytes) -> Path:
    """An original test image: a PNG of the given RGBA bytes."""

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    rows = b"".join(b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    path.write_bytes(png)
    return path


def loaded(svg: win.Svg) -> win.NativeHandle:
    handle = svg.handle  # a property: the stub cannot know load_string() changed it
    assert handle is not None
    return handle


def alive(handle: win.NativeHandle) -> bool:
    return handle.alive  # read again: mypy would narrow a repeated property read


def contexts(window_: win.Window) -> tuple[win.NativeHandle, win.NativeHandle]:
    vg, fb_vg = window_.vg, window_.fb_vg
    assert vg is not None and fb_vg is not None
    return vg, fb_vg


def window() -> win.Window:
    context = raw.context_get()
    assert context is not None and context.window is not None
    return context.window


# NanoVG drawing


def test_paths_fill_where_drawn_top_row_first() -> None:
    def draw(vg: w.GraphicsContext, args: Any) -> None:
        fill_rect(vg, 0, 0, SIZE, 10, RED)  # the top band
        nvg.begin_path(vg)
        nvg.circle(vg, 30, 30, 6)
        nvg.fill_color(vg, BLUE)
        nvg.fill(vg)
        nvg.begin_path(vg)
        nvg.move_to(vg, 2, 38)
        nvg.line_to(vg, 18, 38)
        nvg.stroke_width(vg, 3)
        nvg.stroke_color(vg, GREEN)
        nvg.stroke(vg)

    pixels = paint(draw)
    assert near(pixels.at(20, 5), (255, 0, 0, 255))
    assert near(pixels.at(30, 30), (0, 0, 255, 255))
    assert near(pixels.at(10, 38), (0, 255, 0, 255))
    assert pixels.at(20, 20) == (0, 0, 0, 0)  # untouched: Rack clears framebuffers to transparent
    assert pixels.at(36, 30) == (0, 0, 0, 0)  # outside the circle


def test_paths_curves_and_winding() -> None:
    def draw(vg: w.GraphicsContext, args: Any) -> None:
        nvg.begin_path(vg)
        nvg.rounded_rect(vg, 2, 2, 36, 36, 6)
        nvg.circle(vg, 20, 20, 8)
        nvg.path_winding(vg, nvg.NVG_HOLE)  # the circle cuts a hole
        nvg.fill_color(vg, WHITE)
        nvg.fill(vg)

    pixels = paint(draw)
    assert near(pixels.at(6, 20), (255, 255, 255, 255))
    assert pixels.at(20, 20)[3] == 0
    assert pixels.at(2.2, 2.2)[3] < 128  # the rounded corner

    def shapes(vg: w.GraphicsContext, args: Any) -> None:
        nvg.begin_path(vg)
        nvg.move_to(vg, 0, 0)
        nvg.bezier_to(vg, 10, 0, 30, 0, 40, 0)
        nvg.quad_to(vg, 40, 20, 40, 40)
        nvg.arc_to(vg, 0, 40, 0, 0, 5)
        nvg.close_path(vg)
        nvg.ellipse(vg, 20, 20, 1, 1)
        nvg.arc(vg, 20, 20, 1, 0, nvg.NVG_PI, nvg.NVG_CW)
        nvg.rounded_rect_varying(vg, 0, 0, 1, 1, 0, 0, 0, 0)
        nvg.fill_color(vg, GREEN)
        nvg.fill(vg)

    assert near(paint(shapes).at(20, 30), (0, 255, 0, 255))


def test_state_transforms_and_scissor() -> None:
    def draw(vg: w.GraphicsContext, args: Any) -> None:
        before = nvg.current_transform(vg)
        nvg.save(vg)
        nvg.translate(vg, 20, 0)
        after = nvg.current_transform(vg)
        assert after[4] - before[4] == pytest.approx(20 * before[0]) and after[5] == pytest.approx(before[5])
        fill_rect(vg, 0, 0, 10, 10, GREEN)  # lands at x 20..30
        nvg.restore(vg)
        nvg.save(vg)
        nvg.scissor(vg, 0, 20, 10, 20)
        fill_rect(vg, 0, 20, SIZE, 20, RED)  # only x 0..10 survives
        nvg.reset_scissor(vg)
        nvg.intersect_scissor(vg, 0, 0, SIZE, SIZE)
        nvg.restore(vg)
        with pytest.raises(OwnershipError, match="restore"):
            nvg.restore(vg)  # would pop Rack's own saved state

    pixels = paint(draw)
    assert near(pixels.at(25, 5), (0, 255, 0, 255)) and pixels.at(5, 5)[3] == 0
    assert near(pixels.at(5, 30), (255, 0, 0, 255)) and pixels.at(20, 30)[3] == 0


def test_unrestored_saves_end_with_their_callback() -> None:
    def unbalanced(vg: w.GraphicsContext, args: Any) -> None:
        nvg.save(vg)
        nvg.translate(vg, 30, 30)
        nvg.global_alpha(vg, 0.1)

    first = Canvas(unbalanced)
    second = Canvas(lambda vg, args: fill_rect(vg, 0, 0, 10, 10, RED))
    pixels = render(first, second)
    # Rack's drawChild restores around each child anyway; RackPy also restores what Python saved.
    assert near(pixels.at(5, 5), (255, 0, 0, 255))

    def nested(vg: w.GraphicsContext, args: Any) -> None:
        for _ in range(5):
            nvg.save(vg)  # never restored: undone when the callback ends
        nvg.translate(vg, 100, 100)

    class Parent(w.Widget):
        def draw(self, args: Any) -> None:
            nvg.save(args.vg)
            super().draw(args)  # Rack draws the children
            nvg.restore(args.vg)
            fill_rect(args.vg, 30, 0, 10, 10, BLUE)

    parent = Parent()
    parent.box = Rect(Vec(), Vec(SIZE, SIZE))
    child = Canvas(nested)
    child.box = Rect(Vec(), Vec(SIZE, SIZE))
    parent.add_child(child)
    pixels = render(parent)
    assert near(pixels.at(35, 5), (0, 0, 255, 255))  # the child's saves did not shift the parent


def test_graphics_contexts_are_scoped_to_their_callback() -> None:
    kept: list[Any] = []
    errors: list[str] = []

    class Parent(w.Widget):
        def draw(self, args: Any) -> None:
            kept.append(args.vg)
            kept.append(w.DrawArgs(vg=args.vg))  # an owned copy still has this callback's context
            super().draw(args)

    def child_draw(vg: w.GraphicsContext, args: Any) -> None:
        try:
            nvg.begin_path(kept[0])  # the enclosing callback's context, while a child draws
        except OwnershipError:
            errors.append("enclosing")

    parent = Parent()
    parent.box = Rect(Vec(), Vec(SIZE, SIZE))
    child = Canvas(child_draw)
    child.box = parent.box.copy()
    parent.add_child(child)
    render(parent)
    assert errors == ["enclosing"]
    with pytest.raises(StaleHandleError):
        nvg.begin_path(kept[0])  # the callback has ended
    with pytest.raises(StaleHandleError):
        _ = kept[1].vg

    def on_create(event: Any) -> None:
        with pytest.raises(OwnershipError, match="not a drawing callback"):
            nvg.begin_path(event.vg)
        errors.append("create")

    class Listener(w.Widget):
        def on_context_create(self, event: Any) -> None:
            on_create(event)
            super().on_context_create(event)

    listener = Listener()
    listener.attach_to_scene()
    try:
        HOOKS.recreate_graphics_context()
    finally:
        listener.remove_from_parent()
        listener.close()
    assert errors[-1] == "create"


def test_paints_gradients_and_composite_operations() -> None:
    def draw(vg: w.GraphicsContext, args: Any) -> None:
        linear = nvg.linear_gradient(vg, 0, 0, SIZE, 0, nvg.rgb(0, 0, 0), WHITE)
        assert linear.inner_color == nvg.rgb(0, 0, 0) and linear.outer_color == WHITE and linear.image == 0
        nvg.begin_path(vg)
        nvg.rect(vg, 0, 0, SIZE, 10)
        nvg.fill_paint(vg, linear)
        nvg.fill(vg)
        radial = nvg.radial_gradient(vg, 20, 25, 1, 5, RED, nvg.rgba(255, 0, 0, 0))
        nvg.begin_path(vg)
        nvg.circle(vg, 20, 25, 6)
        nvg.fill_paint(vg, radial)
        nvg.fill(vg)
        box = nvg.box_gradient(vg, 0, 32, 10, 8, 2, 1, GREEN, GREEN)
        nvg.begin_path(vg)
        nvg.rect(vg, 0, 32, 10, 8)
        nvg.stroke_paint(vg, box)
        nvg.fill_paint(vg, box)
        nvg.fill(vg)
        nvg.global_composite_operation(vg, nvg.NVG_DESTINATION_OUT)
        fill_rect(vg, 0, 36, 5, 4, WHITE)  # erases
        nvg.global_composite_operation(vg, nvg.NVG_SOURCE_OVER)
        nvg.global_composite_blend_func(vg, nvg.NVG_ONE, nvg.NVG_ONE_MINUS_SRC_ALPHA)
        nvg.global_composite_blend_func_separate(vg, nvg.NVG_ONE, nvg.NVG_ONE_MINUS_SRC_ALPHA, nvg.NVG_ONE, nvg.NVG_ONE)

    pixels = paint(draw)
    left, right = pixels.at(2, 5), pixels.at(38, 5)
    assert left[0] < 40 and right[0] > 215 and left[3] == right[3] == 255
    assert near(pixels.at(20, 25), (255, 0, 0, 255))
    assert near(pixels.at(8, 33), (0, 255, 0, 255))
    assert pixels.at(2, 38)[3] == 0


def test_alpha_and_tint() -> None:
    def draw(vg: w.GraphicsContext, args: Any) -> None:
        nvg.global_alpha(vg, 0.5)
        fill_rect(vg, 0, 0, 20, 20, WHITE)
        nvg.global_alpha(vg, 1)
        nvg.global_tint(vg, nvg.rgbaf(1, 0, 0, 1))  # Rack's NanoVG: multiplies every colour
        assert nvg.get_global_tint(vg) == nvg.rgbaf(1, 0, 0, 1)
        nvg.tint(vg, nvg.rgbaf(1, 1, 1, 1))
        nvg.alpha(vg, 1)
        fill_rect(vg, 20, 0, 20, 20, WHITE)
        nvg.reset(vg)
        nvg.shape_anti_alias(vg, False)
        nvg.miter_limit(vg, 4)
        nvg.line_cap(vg, nvg.NVG_ROUND)
        nvg.line_join(vg, nvg.NVG_BEVEL)
        fill_rect(vg, 0, 20, 20, 20, WHITE)  # reset() removed the tint

    pixels = paint(draw)
    assert near(pixels.at(10, 10), (128, 128, 128, 128), 2)  # premultiplied half-transparent white
    assert near(pixels.at(30, 10), (255, 0, 0, 255))
    assert near(pixels.at(10, 30), (255, 255, 255, 255))


def test_pure_colour_and_transform_functions() -> None:
    assert nvg.rgb(255, 0, 0) == nvg.NVGcolor(1, 0, 0, 1)
    assert nvg.rgba(0, 0, 0, 0) == nvg.rgbaf(0, 0, 0, 0) and nvg.rgbf(0, 1, 0) == GREEN
    assert nvg.lerp_rgba(RED, BLUE, 0.5) == nvg.NVGcolor(0.5, 0, 0.5, 1)
    assert nvg.trans_rgba(RED, 0).a == 0 and nvg.trans_rgbaf(RED, 0.25).a == 0.25
    assert nvg.hsl(0, 1, 0.5).rgba == pytest.approx(RED.rgba, abs=1e-6) and nvg.hsla(0, 1, 0.5, 0).a == 0
    with pytest.raises(TypeError):
        nvg.rgb(256, 0, 0)
    assert nvg.transform_identity() == (1, 0, 0, 1, 0, 0)
    move, grow = nvg.transform_translate(3, 4), nvg.transform_scale(2, 2)
    assert nvg.transform_point(nvg.transform_multiply(move, grow), 1, 1) == (8, 10)  # move, then grow
    assert nvg.transform_point(nvg.transform_premultiply(move, grow), 1, 1) == (5, 6)  # grow, then move
    inverse = nvg.transform_inverse(move)
    assert inverse is not None and nvg.transform_point(inverse, 3, 4) == (0, 0)
    assert nvg.transform_inverse(nvg.transform_scale(0, 0)) is None
    turn = nvg.transform_rotate(math.pi / 2)
    assert nvg.transform_point(turn, 1, 0) == pytest.approx((0, 1), abs=1e-6)
    assert nvg.transform_skew_x(0) == nvg.transform_skew_y(0) == nvg.transform_identity()
    assert nvg.deg_to_rad(180) == pytest.approx(math.pi) and nvg.rad_to_deg(math.pi) == pytest.approx(180)


def test_text_measurement_and_drawing() -> None:
    font = window().load_font(asset.system(FONT))
    assert font is not None and font.handle >= 0
    emoji = window().load_font_without_fallbacks(asset.system("res/fonts/NotoEmoji-Medium.ttf"))
    bold = window().load_font_without_fallbacks(asset.system("res/fonts/Nunito-Bold.ttf"))
    assert emoji is not None and bold is not None
    seen: dict[str, Any] = {}

    def draw(vg: w.GraphicsContext, args: Any) -> None:
        nvg.text_metrics(vg)  # no font yet: zeros, no error
        nvg.font_face_id(vg, font)
        nvg.font_size(vg, 20)
        nvg.font_blur(vg, 0)
        nvg.text_letter_spacing(vg, 0)
        nvg.text_line_height(vg, 1)
        nvg.text_align(vg, nvg.NVG_ALIGN_LEFT | nvg.NVG_ALIGN_TOP)
        nvg.fill_color(vg, WHITE)
        seen["advance"] = nvg.text(vg, 2, 2, "Hi")
        seen["bounds"] = nvg.text_bounds(vg, 2, 2, "Hi")
        seen["glyphs"] = nvg.text_glyph_positions(vg, 0, 0, "é€x")
        seen["rows"] = nvg.text_break_lines(vg, "aa bb cc", 30)
        seen["box"] = nvg.text_box_bounds(vg, 0, 0, 30, "aa bb cc")
        seen["metrics"] = nvg.text_metrics(vg)
        nvg.text_box(vg, 0, 24, 30, "aa bb")
        seen["empty"] = (
            nvg.text_bounds(vg, 0, 0, ""),
            nvg.text_glyph_positions(vg, 0, 0, ""),
            nvg.text_break_lines(vg, "", 9),
        )
        seen["found"] = nvg.find_font(vg, "DejaVuSans")
        nvg.font_face(vg, "DejaVuSans")
        assert nvg.add_fallback_font(vg, "DejaVuSans", "nope") == 0  # NanoVG refuses an unknown name
        with pytest.raises(ValueError, match="no font named"):
            nvg.reset_fallback_fonts(vg, "nope")  # NanoVG itself would crash
        with pytest.raises(ValueError, match="not a font loaded"):
            nvg.add_fallback_font_id(vg, font, 10**6)  # fontstash would store it and crash drawing text
        with pytest.raises(ValueError, match="not a font loaded"):
            nvg.reset_fallback_fonts_id(vg, -1)
        assert nvg.add_fallback_font_id(vg, bold, emoji.handle) == 1
        nvg.reset_fallback_fonts_id(vg, bold)
        with pytest.raises(ValueError, match="3276"):
            nvg.font_size(vg, 1e6)
        with pytest.raises(ValueError):
            nvg.font_blur(vg, math.nan)

    pixels = paint(draw)
    advance, (xmin, ymin, xmax, ymax) = seen["bounds"]
    assert seen["advance"] == pytest.approx(2 + advance) and advance > 0  # text() returns x after the text
    assert xmin >= 1 and xmax > xmin and ymax > ymin
    assert [g.index for g in seen["glyphs"]] == [0, 1, 2]  # character indices, not UTF-8 bytes
    assert seen["glyphs"][1].x > seen["glyphs"][0].x
    rows = seen["rows"]
    assert len(rows) >= 2 and rows[0].start == 0 and rows[-1].end == 8 and rows[0].width <= 30
    assert seen["box"][3] > seen["box"][1]
    ascender, descender, line = seen["metrics"]
    assert ascender > 0 > descender and line > 0
    assert seen["empty"][0][0] == 0 and seen["empty"][1:] == ([], [])
    assert seen["found"] >= 0
    assert pixels.lit() > 20  # text drew something


def test_images_and_patterns(tmp_path: Path) -> None:
    # A 2 x 2 image: red, green / blue, white.
    data = bytes([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255])
    path = write_png(tmp_path / "quad.png", 2, 2, data)
    image = window().load_image(str(path))
    assert image is not None and image.handle > 0 and window().load_image(str(path)) is image
    assert window().load_image(str(tmp_path / "missing.png")) is None  # Rack warns and caches None
    sizes: list[tuple[int, int]] = []

    def draw(vg: w.GraphicsContext, args: Any) -> None:
        sizes.append(nvg.image_size(vg, image))
        sizes.append(nvg.image_size(vg, 999999))
        pattern = nvg.image_pattern(vg, 0, 0, SIZE, SIZE, 0, image, 1)
        assert pattern.image == image.handle
        nvg.begin_path(vg)
        nvg.rect(vg, 0, 0, SIZE, SIZE)
        nvg.fill_paint(vg, pattern)
        nvg.fill(vg)

    pixels = paint(draw)
    assert sizes == [(2, 2), (0, 0)]
    # Texel centres (Rack loads images linearly filtered and repeating).
    assert near(pixels.at(10, 10), (255, 0, 0, 255), 24) and near(pixels.at(30, 10), (0, 255, 0, 255), 24)
    assert near(pixels.at(10, 30), (0, 0, 255, 255), 24) and near(pixels.at(30, 30), (255, 255, 255, 255), 24)


def test_svg_handles_and_svg_draw() -> None:
    svg = win.Svg()
    assert svg.handle is None
    svg.load_string(
        '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40">'
        '<rect width="40" height="20" fill="#00ff00"/></svg>'
    )
    handle = loaded(svg)
    assert handle.kind == "NSVGimage" and handle.alive
    pixels = paint(lambda vg, args: win.svg_draw(vg, handle))
    assert near(pixels.at(20, 5), (0, 255, 0, 255)) and pixels.at(20, 30)[3] == 0
    svg.load_string('<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"/>')
    assert not alive(handle)  # load_string replaced the parsed image
    errors: list[type[BaseException]] = []

    def stale(vg: w.GraphicsContext, args: Any) -> None:
        try:
            win.svg_draw(vg, handle)
        except StaleHandleError:
            errors.append(StaleHandleError)

    paint(stale)
    assert errors == [StaleHandleError]
    with pytest.warns(DeprecationWarning, match="window.Svg"):
        assert win.SVG is win.Svg


# rack::window resources


def test_context_window_and_native_handles() -> None:
    context = raw.context_get()
    assert context is not None
    window = context.window
    assert window is not None and window is context.window and window.alive
    assert window.vg is not None and window.fb_vg is not None and window.vg is not window.fb_vg
    assert window.vg is window.vg and window.vg.kind == "NVGcontext" and window.vg.alive
    assert window.win.kind == "GLFWwindow" and window.win.alive
    patch, loopback = context.patch, context.midi_loopback_context
    assert patch is not None and patch.kind == "patch::Manager" and patch.alive
    assert loopback is not None and loopback.kind == "midiloopback::Context"
    assert "stale" not in repr(window.vg)
    with pytest.warns(DeprecationWarning, match="context_get"):
        assert raw.app_get() is context


def test_window_queries_and_controls() -> None:
    window_ = window()
    size = window_.get_size()
    assert size.x > 0 and size.y > 0
    window_.set_size(size)  # asynchronous under X11; at least 640 x 480
    with pytest.raises(ValueError):
        window_.set_size(Vec(math.inf, 1))
    ratio = window_.pixel_ratio
    assert ratio > 0 and window_.window_ratio > 0
    window_.pixel_ratio = ratio
    with pytest.raises(ValueError):
        window_.pixel_ratio = 0
    with pytest.raises(ValueError):
        window_.window_ratio = math.nan
    assert window_.get_mods() == 0  # nothing held under Xvfb
    assert window_.get_monitor_refresh_rate() > 0
    assert math.isfinite(window_.get_frame_time()) and window_.get_last_frame_duration() >= 0
    assert math.isfinite(window_.get_frame_duration_remaining())
    window_.cursor_lock()
    assert window_.is_cursor_locked()
    window_.cursor_unlock()
    assert not window_.is_cursor_locked()
    assert not window_.is_full_screen()
    window_.set_full_screen(True)
    try:
        assert window_.is_full_screen()
    finally:
        window_.set_full_screen(False)
    assert not window_.is_full_screen()
    window_.close()
    try:
        assert HOOKS.window_should_close()  # Rack would quit after this frame
    finally:
        HOOKS.cancel_window_close()


def test_window_fonts_are_cached_and_checked() -> None:
    window_ = window()
    font = window_.load_font(asset.system(FONT))
    assert font is not None and font is window_.load_font(asset.system(FONT)) and font.alive
    assert font.vg is window_.vg
    plain = window_.load_font_without_fallbacks(asset.system("res/fonts/NotoEmoji-Medium.ttf"))
    assert plain is not None and plain.handle >= 0 and plain.handle != font.handle
    assert window_.load_font("/nonexistent/font.ttf") is None
    ui_font = window_.ui_font
    assert ui_font is not None and ui_font.handle >= 0
    window_.ui_font = font
    try:
        assert window_.ui_font is font
    finally:
        window_.ui_font = ui_font
    with pytest.raises(TypeError):
        window_.ui_font = None  # type: ignore[assignment]  # Rack draws with it every frame
    unloaded = win.Font()
    assert unloaded.handle == -1 and unloaded.vg is None
    with pytest.raises(ValueError, match="loaded"):
        window_.ui_font = unloaded
    with pytest.raises(OwnershipError):
        font.load_file(asset.system(FONT), contexts(window_)[0])
    own = win.Font()
    own.load_file(asset.system(FONT), contexts(window_)[0])
    assert own.handle >= 0 and own.vg is window_.vg
    with pytest.raises(raw.Exception):
        win.Font().load_file("/nonexistent/font.ttf", contexts(window_)[0])
    with pytest.warns(DeprecationWarning, match="load_font"):
        assert win.Font.load(asset.system(FONT)) is font
    loaded = window_.load_svg(asset.system("res/ComponentLibrary/ScrewSilver.svg"))
    assert loaded is not None and loaded is window_.load_svg(asset.system("res/ComponentLibrary/ScrewSilver.svg"))


def test_python_owned_images_free_their_textures(tmp_path: Path) -> None:
    path = write_png(tmp_path / "one.png", 1, 1, bytes([1, 2, 3, 255]))
    window_ = window()
    sizes: list[tuple[int, int]] = []

    def size_of(handle: int) -> tuple[int, int]:
        paint(lambda vg, args: sizes.append(nvg.image_size(vg, handle)))
        return sizes[-1]

    image = win.Image()
    assert image.handle == -1 and image.vg is None
    image.load_file(str(path), contexts(window_)[0])
    handle = image.handle
    assert handle > 0 and size_of(handle) == (1, 1)
    del image
    gc.collect()
    assert size_of(handle) == (0, 0)  # dropped on the main thread: texture deleted

    threaded = win.Image()
    threaded.load_file(str(path), contexts(window_)[1])
    handle = threaded.handle
    holder = [threaded]
    del threaded
    worker = threading.Thread(target=holder.clear)
    worker.start()
    worker.join()
    gc.collect()
    assert size_of(handle) == (1, 1)  # dropped elsewhere: deleted at the next UI step, on the main thread

    with pytest.raises(raw.Exception):
        win.Image().load_file(str(tmp_path / "missing.png"), contexts(window_)[0])
    cached = window_.load_image(str(path))
    assert cached is not None
    with pytest.raises(OwnershipError):
        cached.load_file(str(path), contexts(window_)[0])
    with pytest.warns(DeprecationWarning, match="load_image"):
        assert win.Image.load(str(path)) is cached


def test_resources_go_stale_when_the_graphics_context_is_destroyed(tmp_path: Path) -> None:
    path = write_png(tmp_path / "two.png", 1, 1, bytes([9, 9, 9, 255]))
    window_ = window()
    font = window_.load_font(asset.system(FONT))
    cached = window_.load_image(str(path))
    own = win.Image()
    own.load_file(str(path), contexts(window_)[0])
    owned_handle = own.handle
    vg = window_.vg
    assert font is not None and cached is not None and vg is not None
    HOOKS.recreate_graphics_context()  # Rack's ContextDestroy, then ContextCreate
    for resource in (font, cached, own):
        assert not resource.alive
        with pytest.raises(StaleHandleError, match="load it again"):
            _ = resource.handle
    assert not vg.alive and "stale" in repr(vg)
    # Load again, as Rack advises after a context change.
    again = window_.load_font(asset.system(FONT))
    assert again is not None and again.alive and again is not font and window_.vg is not vg
    sizes: list[tuple[int, int]] = []
    paint(lambda v, args: sizes.append(nvg.image_size(v, owned_handle)))
    assert sizes == [(0, 0)]  # the Python-owned texture was deleted before the context went


def test_context_set_on_python_threads() -> None:
    context = raw.context_get()
    assert context is not None
    raw.context_set(context)  # the main thread's own context: accepted
    with pytest.raises(ContextError):
        raw.context_set(None)
    seen: list[object] = []

    def worker() -> None:
        seen.append(raw.context_get())
        raw.context_set(context)
        seen.append(raw.context_get())
        raw.context_set(None)
        seen.append(raw.context_get())

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    assert seen == [None, context, None]


def test_unit_helpers() -> None:
    assert win.SVG_DPI == 75 and pytest.approx(25.4) == win.MM_PER_IN
    assert win.in2px(2) == 150 and win.in2px(Vec(1, 2)) == Vec(75, 150)
    assert win.mm2px(25.4) == pytest.approx(75) and win.mm2px(Vec(25.4, 0)).x == pytest.approx(75)


def test_resources_held_past_rack_window_teardown(tmp_path: Path) -> None:
    # Kept until Rack quits on purpose. Rack's ~Window deletes its NanoVG contexts; a Python-owned
    # Image deleting its texture afterwards would use a freed context. RackPy deletes it first.
    path = write_png(tmp_path / "kept.png", 1, 1, bytes([5, 5, 5, 255]))
    window_ = window()
    own = win.Image()
    own.load_file(str(path), contexts(window_)[0])
    kept = (own, window_.load_font(asset.system(FONT)), window_, raw.context_get())
    original_stop = rackpy._bootstrap.stop

    def stop() -> None:
        original_stop()
        image, font, window_after, context = kept
        assert font is not None and context is not None
        assert not image.alive and not font.alive and not window_after.alive and not context.alive
        with pytest.raises(StaleHandleError):
            _ = image.handle
        _rackpy_host.log_info("window-resources-teardown-ok")

    rackpy._bootstrap.stop = stop


def test_runnable_drawing_snippet() -> None:
    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "drawing.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    assert scope["exercise_drawing"]() is True
