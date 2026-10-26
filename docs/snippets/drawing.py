# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from typing import Any

from rackpy import raw
from rackpy.raw import asset
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


class Meter(w.Widget):
    """A level bar with a caption, drawn with NanoVG in Rack's draw() callback."""

    def __init__(self, level: float) -> None:
        super().__init__()
        self.level = level

    def draw(self, args: Any) -> None:
        vg = args.vg  # valid only in this call
        width, height = self.box.size.x, self.box.size.y
        nvg.begin_path(vg)
        nvg.rounded_rect(vg, 0, 0, width, height, 3)
        nvg.fill_color(vg, nvg.rgb(20, 24, 32))
        nvg.fill(vg)
        nvg.begin_path(vg)
        nvg.rect(vg, 2, 2, (width - 4) * self.level, height - 4)
        nvg.fill_paint(vg, nvg.linear_gradient(vg, 0, 0, width, 0, nvg.rgb(40, 200, 120), nvg.rgb(250, 200, 40)))
        nvg.fill(vg)
        context = raw.context_get()
        font = (
            context.window.load_font(asset.system("res/fonts/DejaVuSans.ttf")) if context and context.window else None
        )
        if font is not None:  # load fonts each frame: Rack caches them, and they expire with the window
            nvg.font_face_id(vg, font)
            nvg.font_size(vg, 10)
            nvg.text_align(vg, nvg.NVG_ALIGN_CENTER | nvg.NVG_ALIGN_MIDDLE)
            nvg.fill_color(vg, nvg.rgb(255, 255, 255))
            nvg.text(vg, width / 2, height / 2, f"{self.level:.0%}")


def exercise_drawing() -> bool:
    """Call from Rack's UI thread after its context exists, not plugin startup."""
    frame = w.FramebufferWidget()
    meter = Meter(0.6)
    frame.box = meter.box = Rect(Vec(0, 0), Vec(60, 14))
    frame.add_child(meter)
    try:
        frame.render()  # Rack renders the frame, calling Meter.draw
        return frame.get_framebuffer() is not None
    finally:
        frame.close()
