# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-27B: the rackpy.drawing facade, and a guide drawing compared with a reviewed reference image.

The reference, tests/rendering/reference/guide.png, is RackPy's own drawing
(GPL-3.0-or-later like the rest of the repository), rendered by Rack Free
2.6.6 with Mesa's software OpenGL under Xvfb and reviewed by eye. Set
RACKPY_UPDATE_REFERENCE_IMAGES=1 to rewrite it; the test then fails so the new
image is reviewed before it is committed.
"""

import os
import struct
import zlib
from pathlib import Path
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import drawing
from rackpy.drawing import Painter
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec

WIDTH, HEIGHT = 120, 80
REFERENCE = Path(__file__).resolve().parents[1] / "reference" / "guide.png"


class Canvas(w.Widget):
    def __init__(self, paint: Any) -> None:
        super().__init__()
        self.paint = paint

    def draw(self, args: Any) -> None:
        self.paint(Painter(args))


def render(widget: w.Widget, width: int = WIDTH, height: int = HEIGHT) -> tuple[int, int, bytes]:
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), Vec(width, height))
    if not widget.box.size.is_finite():
        widget.box = Rect(Vec(), Vec(width, height))
    frame.add_child(widget)
    try:
        frame.render()
        return HOOKS.framebuffer_pixels(frame)
    finally:
        frame.close()


def at(image: tuple[int, int, bytes], x: int, y: int) -> tuple[int, ...]:
    width, _, data = image
    i = (y * width + x) * 4
    return tuple(data[i : i + 4])


def near(actual: tuple[int, ...], expected: tuple[int, ...], tolerance: int = 8) -> bool:
    return all(abs(a - e) <= tolerance for a, e in zip(actual, expected, strict=True))


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    rows = b"".join(b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    )


def read_png(path: Path) -> tuple[int, int, bytes]:
    """Reads the PNGs write_png makes (8-bit RGBA, unfiltered rows)."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        kind, body = data[pos + 4 : pos + 8], data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height = struct.unpack(">II", body[:8])
            assert body[8:] == bytes([8, 6, 0, 0, 0])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4 + 1
    assert all(raw[y * stride] == 0 for y in range(height))
    return width, height, b"".join(raw[y * stride + 1 : (y + 1) * stride] for y in range(height))


# The facade


def test_colours() -> None:
    assert drawing.color("#ff0000") == nvg.rgb(255, 0, 0)
    assert drawing.color("00ff0080") == nvg.rgba(0, 255, 0, 128)
    assert drawing.color((0, 0, 1)) == nvg.rgbf(0, 0, 1) and drawing.color((1, 1, 1, 0.5)).a == 0.5
    assert drawing.color(nvg.rgb(1, 2, 3)) == nvg.rgb(1, 2, 3)
    for bad in ("#ff00", "#gg0000", "red"):
        with pytest.raises(ValueError):
            drawing.color(bad)
    with pytest.raises(TypeError):
        drawing.color((1, 2))  # type: ignore[arg-type]


def test_paths_state_and_gradients() -> None:
    def paint(p: Painter) -> None:
        p.rect(0, 0, 40, 40).fill("#ff0000")
        with p.saved():
            p.translate(40, 0).scale(2).rotate(0)
            p.rect(0, 0, 10, 10).fill((0, 1, 0))
        with p.clipped(0, 40, 20, 40):
            p.rect(0, 40, 80, 40).fill(nvg.rgb(0, 0, 255))
        p.line(90, 0, 90, 40).stroke("#ffffff", width=4)
        p.path().move_to(100, 0).line_to(119, 0).line_to(119, 39).close().fill(
            p.linear_gradient(100, 0, 119, 0, "#000000", "#ffffff")
        )
        p.circle(60, 60, 10).fill(p.radial_gradient(60, 60, 0, 10, "#ffff00", "#ffff00"))
        p.path().rect(80, 50, 20, 20, radius=4).circle(90, 60, 4).hole().fill(
            p.box_gradient(80, 50, 20, 20, 4, 1, "#00ffff", "#00ffff")
        )
        with p.saved():
            p.alpha(0.5)
            p.rect(100, 50, 20, 20).fill("#ffffff")
        p.path().arc(110, 30, 5, 0, 3.14159, clockwise=True).ellipse(10, 70, 2, 1).quad_to(1, 1, 2, 2).bezier_to(
            1, 1, 2, 2, 3, 3
        )
        p.path().arc_to(0, 0, 1, 1, 1).stroke("#000000")

    image = render(Canvas(paint))
    assert near(at(image, 20, 20), (255, 0, 0, 255))
    assert near(at(image, 55, 15), (0, 255, 0, 255))  # translated by 40, scaled by 2
    assert near(at(image, 10, 60), (0, 0, 255, 255)) and at(image, 40, 60)[3] == 0  # clipped at x 20
    assert near(at(image, 90, 20), (255, 255, 255, 255))
    assert at(image, 106, 10)[0] < at(image, 117, 10)[0]  # the gradient brightens to the right
    assert near(at(image, 60, 60), (255, 255, 0, 255))
    assert near(at(image, 83, 60), (0, 255, 255, 255)) and at(image, 90, 60)[3] == 0  # the hole
    assert near(at(image, 110, 60), (128, 128, 128, 128), 3)  # half alpha, premultiplied


def test_text_measurement_and_wrapping() -> None:
    seen: dict[str, Any] = {}

    def paint(p: Painter) -> None:
        seen["bounds"] = p.measure("Rack", 10, 20, size=14)
        seen["centred"] = p.measure("Rack", 60, 20, size=14, align="center middle")
        seen["end"] = p.text("Rack", 10, 20, size=14, color="#ffffff")
        seen["box"] = p.text("one two three four", 7, 50, size=10, width=40)
        for bad in ("centre", "left right", "top middle"):
            with pytest.raises(ValueError):
                p.text("x", 0, 0, align=bad)
        with pytest.raises(FileNotFoundError):
            p.text("x", 0, 0, font="res/fonts/Missing.ttf")

    image = render(Canvas(paint))
    bounds, centred = seen["bounds"], seen["centred"]
    assert isinstance(bounds, drawing.TextBounds) and bounds.width > 20 and bounds.height > 8
    assert seen["end"] == pytest.approx(10 + bounds.advance)
    assert centred.xmin < 60 < centred.xmax and centred.ymin < 20 < centred.ymax
    assert seen["box"] == 47  # wrapped text returns x + box width, not the rendered advance
    assert any(at(image, x, y)[3] for x in range(10, 50) for y in range(8, 22))  # the text drew


def test_images_through_the_facade(tmp_path: Path) -> None:
    path = tmp_path / "blue.png"
    write_png(path, 1, 1, bytes([0, 0, 255, 255]))
    image = render(Canvas(lambda p: p.image(path, 0, 0, 20, 20)), 20, 20)
    assert near(at(image, 10, 10), (0, 0, 255, 255))

    def missing(p: Painter) -> None:
        with pytest.raises(FileNotFoundError):
            p.image(tmp_path / "missing.png", 0, 0, 1, 1)

    render(Canvas(missing), 4, 4)


def test_a_painter_expires_with_its_callback() -> None:
    kept: list[Painter] = []
    render(Canvas(kept.append), 4, 4)
    with pytest.raises(StaleHandleError):
        kept[0].rect(0, 0, 1, 1)
    with pytest.raises(TypeError):
        Painter(object())  # type: ignore[arg-type]  # exercise the runtime check for untyped callers

    class Parent(w.Widget):
        def draw(self, args: Any) -> None:
            kept.append(Painter(args))
            super().draw(args)

    def child(p: Painter) -> None:
        with pytest.raises(OwnershipError):
            kept[-1].rect(0, 0, 1, 1)  # the enclosing callback's painter

    parent = Parent()
    parent.box = Rect(Vec(), Vec(4, 4))
    canvas = Canvas(child)
    canvas.box = Rect(Vec(), Vec(4, 4))
    parent.add_child(canvas)
    render(parent, 4, 4)


# The reviewed guide drawing


class Guide(w.Widget):
    """Panel, shapes, a gradient and text on layer 0, and a light on Rack's self-lit layer 1."""

    def draw(self, args: Any) -> None:
        p = Painter(args)
        p.rect(0, 0, WIDTH, HEIGHT, radius=6).fill(p.linear_gradient(0, 0, 0, HEIGHT, "#2b3440", "#141a22"))
        p.rect(6, 6, 50, 30, radius=3).stroke("#8fa3b8", width=1.5)
        p.path().circle(31, 21, 11).circle(31, 21, 5).hole().fill("#e0b040")
        p.line(64, 10, 112, 34).stroke("#60c0a0", width=3)
        p.text("RackPy", WIDTH / 2, 58, size=16, color="#f0f0f0", align="center middle")

    def draw_layer(self, args: Any, layer: int) -> None:
        if layer == 1:
            p = Painter(args)
            p.circle(108, 70, 5).fill(p.radial_gradient(108, 70, 1, 5, "#ff3030", "#ff303000"))


class Room(w.Widget):
    """Composes like Rack's RackWidget: layer 0, the room dimming, then layer 1 undimmed."""

    def draw(self, args: Any) -> None:
        super().draw(args)
        Painter(args).rect(0, 0, WIDTH, HEIGHT).fill((0, 0, 0, 0.5))
        for child in self.children:
            self.draw_child(child, args, 1)


def test_guide_drawing_matches_the_reviewed_reference(tmp_path: Path) -> None:
    room, guide = Room(), Guide()
    room.box = guide.box = Rect(Vec(), Vec(WIDTH, HEIGHT))
    room.add_child(guide)
    width, height, data = render(room)
    assert (width, height) == (WIDTH, HEIGHT), "the reference assumes pixel ratio 1"
    if os.environ.get("RACKPY_UPDATE_REFERENCE_IMAGES") == "1":
        REFERENCE.parent.mkdir(exist_ok=True)
        write_png(REFERENCE, width, height, data)
        pytest.fail(f"rewrote {REFERENCE}: review it, then run again without RACKPY_UPDATE_REFERENCE_IMAGES")
    _, _, expected = read_png(REFERENCE)
    differences = [
        max(abs(a - b) for a, b in zip(data[i : i + 4], expected[i : i + 4], strict=True))
        for i in range(0, len(data), 4)
    ]
    actual = tmp_path / "guide-actual.png"
    write_png(actual, width, height, data)
    assert sum(d > 24 for d in differences) <= len(differences) // 200, f"over 0.5% of pixels differ; see {actual}"
    assert sum(differences) / len(differences) <= 2, f"mean difference too large; see {actual}"


def test_runnable_painter_snippet() -> None:
    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "painter.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    assert scope["exercise_painter"]() is True
