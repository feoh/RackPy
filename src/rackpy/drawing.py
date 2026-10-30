# SPDX-License-Identifier: GPL-3.0-or-later
"""Pythonic drawing for widgets' ``draw()`` callbacks, over :mod:`rackpy.raw.nanovg`. Inside Rack only.

``Painter(args)`` wraps the callback's ``GraphicsContext`` and expires with it.
Colours are an ``NVGcolor``, a ``"#RRGGBB"``/``"#RRGGBBAA"`` string, or an
``(r, g, b)``/``(r, g, b, a)`` tuple of 0-1 floats. Fonts and images are
loaded through Rack's window on every call, as Rack advises: its caches make
that cheap, and they expire with the window. Everything here is the raw
layer's behaviour in another shape; nothing has native semantics of its own.
"""

from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Self

from rackpy.raw import asset as _asset
from rackpy.raw import context_get
from rackpy.raw import nanovg as _nvg
from rackpy.raw.nanovg import NVGcolor, NVGpaint
from rackpy.raw.widget import GraphicsContext
from rackpy.raw.window import Font, Image

__all__ = ["DEFAULT_FONT", "Color", "Painter", "Path2D", "TextBounds", "color"]

Color = NVGcolor | str | tuple[float, float, float] | tuple[float, float, float, float]
DEFAULT_FONT = "res/fonts/DejaVuSans.ttf"
"""Rack's UI font, as a path in Rack's system folder."""

_HORIZONTAL = {"left": _nvg.NVG_ALIGN_LEFT, "center": _nvg.NVG_ALIGN_CENTER, "right": _nvg.NVG_ALIGN_RIGHT}
_VERTICAL = {
    "top": _nvg.NVG_ALIGN_TOP,
    "middle": _nvg.NVG_ALIGN_MIDDLE,
    "bottom": _nvg.NVG_ALIGN_BOTTOM,
    "baseline": _nvg.NVG_ALIGN_BASELINE,
}


def color(value: Color) -> NVGcolor:
    """An ``NVGcolor`` from any accepted colour. Unlike Rack's ``fromHexString``, bad text raises ``ValueError``."""
    if isinstance(value, NVGcolor):
        return value
    if isinstance(value, str):
        digits = value.removeprefix("#")
        if len(digits) not in (6, 8) or any(c not in "0123456789abcdefABCDEF" for c in digits):
            raise ValueError(f"not a #RRGGBB or #RRGGBBAA colour: {value!r}")
        parts = [int(digits[i : i + 2], 16) for i in range(0, len(digits), 2)]
        return _nvg.rgba(*parts) if len(parts) == 4 else _nvg.rgb(*parts)
    if isinstance(value, tuple) and len(value) in (3, 4):  # pyright: ignore[reportUnnecessaryIsInstance]
        return NVGcolor(*(float(c) for c in value))
    raise TypeError(f"not a colour: {value!r}")


def _paint(value: Color | NVGpaint, vg: GraphicsContext, stroke: bool) -> None:
    if isinstance(value, NVGpaint):
        (_nvg.stroke_paint if stroke else _nvg.fill_paint)(vg, value)
    else:
        (_nvg.stroke_color if stroke else _nvg.fill_color)(vg, color(value))


def _align(align: str) -> int:
    words = align.split()
    horizontal = [w for w in words if w in _HORIZONTAL]
    vertical = [w for w in words if w in _VERTICAL]
    if len(horizontal) > 1 or len(vertical) > 1 or len(horizontal) + len(vertical) != len(words):
        raise ValueError(f"align takes one of {sorted(_HORIZONTAL)} and one of {sorted(_VERTICAL)}: {align!r}")
    return _HORIZONTAL[horizontal[0] if horizontal else "left"] | _VERTICAL[vertical[0] if vertical else "baseline"]


@dataclass(frozen=True)
class TextBounds:
    """Text measured with :meth:`Painter.measure`: the advance and the box it covers."""

    advance: float
    xmin: float
    ymin: float
    xmax: float
    ymax: float

    @property
    def width(self) -> float:
        return self.xmax - self.xmin

    @property
    def height(self) -> float:
        return self.ymax - self.ymin


class Path2D:
    """A path being built; every method returns the path, and ``fill``/``stroke`` draw it."""

    def __init__(self, vg: GraphicsContext) -> None:
        self._vg = vg
        _nvg.begin_path(vg)

    def move_to(self, x: float, y: float) -> Self:
        _nvg.move_to(self._vg, x, y)
        return self

    def line_to(self, x: float, y: float) -> Self:
        _nvg.line_to(self._vg, x, y)
        return self

    def bezier_to(self, c1x: float, c1y: float, c2x: float, c2y: float, x: float, y: float) -> Self:
        _nvg.bezier_to(self._vg, c1x, c1y, c2x, c2y, x, y)
        return self

    def quad_to(self, cx: float, cy: float, x: float, y: float) -> Self:
        _nvg.quad_to(self._vg, cx, cy, x, y)
        return self

    def arc_to(self, x1: float, y1: float, x2: float, y2: float, radius: float) -> Self:
        _nvg.arc_to(self._vg, x1, y1, x2, y2, radius)
        return self

    def arc(self, cx: float, cy: float, r: float, a0: float, a1: float, *, clockwise: bool = True) -> Self:
        _nvg.arc(self._vg, cx, cy, r, a0, a1, _nvg.NVG_CW if clockwise else _nvg.NVG_CCW)
        return self

    def rect(self, x: float, y: float, width: float, height: float, radius: float = 0) -> Self:
        if radius:
            _nvg.rounded_rect(self._vg, x, y, width, height, radius)
        else:
            _nvg.rect(self._vg, x, y, width, height)
        return self

    def circle(self, cx: float, cy: float, r: float) -> Self:
        _nvg.circle(self._vg, cx, cy, r)
        return self

    def ellipse(self, cx: float, cy: float, rx: float, ry: float) -> Self:
        _nvg.ellipse(self._vg, cx, cy, rx, ry)
        return self

    def close(self) -> Self:
        _nvg.close_path(self._vg)
        return self

    def hole(self) -> Self:
        """Makes the last shape cut a hole in the ones before it."""
        _nvg.path_winding(self._vg, _nvg.NVG_HOLE)
        return self

    def fill(self, paint: Color | NVGpaint) -> Self:
        _paint(paint, self._vg, stroke=False)
        _nvg.fill(self._vg)
        return self

    def stroke(self, paint: Color | NVGpaint, width: float = 1) -> Self:
        _paint(paint, self._vg, stroke=True)
        _nvg.stroke_width(self._vg, width)
        _nvg.stroke(self._vg)
        return self


class Painter:
    """Draws in one drawing callback: ``Painter(args)`` in ``draw``/``draw_layer``, ``Painter(vg)`` in ``draw_vg``."""

    def __init__(self, target: Any) -> None:
        vg = target if isinstance(target, GraphicsContext) else getattr(target, "vg", None)
        if not isinstance(vg, GraphicsContext):
            raise TypeError("Painter needs a drawing callback's DrawArgs or GraphicsContext")
        self.vg: GraphicsContext = vg

    @contextmanager
    def saved(self) -> Iterator[Self]:
        """Saves the render state (transform, style, clip) and restores it after the block."""
        _nvg.save(self.vg)
        try:
            yield self
        finally:
            _nvg.restore(self.vg)

    @contextmanager
    def clipped(self, x: float, y: float, width: float, height: float) -> Iterator[Self]:
        """Draws the block clipped to the rectangle (within any clip already set)."""
        with self.saved():
            _nvg.intersect_scissor(self.vg, x, y, width, height)
            yield self

    def translate(self, x: float, y: float) -> Self:
        _nvg.translate(self.vg, x, y)
        return self

    def scale(self, x: float, y: float | None = None) -> Self:
        _nvg.scale(self.vg, x, x if y is None else y)
        return self

    def rotate(self, angle: float) -> Self:
        _nvg.rotate(self.vg, angle)
        return self

    def alpha(self, alpha: float) -> Self:
        """Multiplies the alpha of what follows (until the state is restored)."""
        _nvg.alpha(self.vg, alpha)
        return self

    def path(self) -> Path2D:
        """Starts a new path."""
        return Path2D(self.vg)

    def rect(self, x: float, y: float, width: float, height: float, radius: float = 0) -> Path2D:
        return self.path().rect(x, y, width, height, radius)

    def circle(self, cx: float, cy: float, r: float) -> Path2D:
        return self.path().circle(cx, cy, r)

    def line(self, x0: float, y0: float, x1: float, y1: float) -> Path2D:
        return self.path().move_to(x0, y0).line_to(x1, y1)

    def linear_gradient(self, x0: float, y0: float, x1: float, y1: float, start: Color, end: Color) -> NVGpaint:
        return _nvg.linear_gradient(self.vg, x0, y0, x1, y1, color(start), color(end))

    def radial_gradient(self, cx: float, cy: float, inner: float, outer: float, start: Color, end: Color) -> NVGpaint:
        return _nvg.radial_gradient(self.vg, cx, cy, inner, outer, color(start), color(end))

    def box_gradient(
        self, x: float, y: float, width: float, height: float, radius: float, feather: float, start: Color, end: Color
    ) -> NVGpaint:
        return _nvg.box_gradient(self.vg, x, y, width, height, radius, feather, color(start), color(end))

    def _font(self, font: str | Path | Font) -> Font:
        if isinstance(font, Font):
            return font
        loaded = None
        context = context_get()
        if context is not None and context.window is not None:
            path = Path(font)
            loaded = context.window.load_font(str(path if path.is_absolute() else _asset.system(str(path))))
        if loaded is None:
            raise FileNotFoundError(f"Rack could not load the font {font}")
        return loaded

    def _text_style(self, size: float, font: str | Path | Font, align: str) -> None:
        _nvg.font_face_id(self.vg, self._font(font))
        _nvg.font_size(self.vg, size)
        _nvg.text_align(self.vg, _align(align))

    def text(
        self,
        text: str,
        x: float,
        y: float,
        *,
        size: float = 12,
        color: Color = "#ffffff",
        font: str | Path | Font = DEFAULT_FONT,
        align: str = "left baseline",
        width: float | None = None,
    ) -> float:
        """Draw text and return the text's ending x. With ``width``, wrap in a box and return ``x + width`` instead.

        ``font`` is a Font, an absolute path, or a path in Rack's system folder; ``align`` is a
        horizontal word (left, center, right) and a vertical one (top, middle, bottom, baseline).
        """
        self._text_style(size, font, align)
        _paint(color, self.vg, stroke=False)
        if width is not None:
            _nvg.text_box(self.vg, x, y, width, text)
            return x + width
        return _nvg.text(self.vg, x, y, text)

    def measure(
        self,
        text: str,
        x: float = 0,
        y: float = 0,
        *,
        size: float = 12,
        font: str | Path | Font = DEFAULT_FONT,
        align: str = "left baseline",
    ) -> TextBounds:
        """Where :meth:`text` would draw ``text``, without drawing it."""
        self._text_style(size, font, align)
        advance, (xmin, ymin, xmax, ymax) = _nvg.text_bounds(self.vg, x, y, text)
        return TextBounds(advance, xmin, ymin, xmax, ymax)

    def image(
        self, image: str | Path | Image, x: float, y: float, width: float, height: float, *, alpha: float = 1
    ) -> None:
        """Draws an image stretched over the rectangle. ``image`` is an Image or a file path."""
        if not isinstance(image, Image):
            context = context_get()
            loaded = context.window.load_image(str(image)) if context is not None and context.window else None
            if loaded is None:
                raise FileNotFoundError(f"Rack could not load the image {image}")
            image = loaded
        pattern = _nvg.image_pattern(self.vg, x, y, width, height, 0, image, alpha)
        self.rect(x, y, width, height).fill(pattern)
