# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.drawing import Painter
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


class Badge(w.Widget):
    """A label on a rounded plate, and a light on Rack's self-lit layer."""

    def __init__(self, label: str) -> None:
        super().__init__()
        self.label = label

    def draw(self, args: w.DrawArgs) -> None:
        p = Painter(args)  # valid only in this call
        width, height = self.box.size.x, self.box.size.y
        p.rect(0, 0, width, height, radius=4).fill(p.linear_gradient(0, 0, 0, height, "#38424e", "#1c222a"))
        with p.saved():
            p.translate(width - 12, height / 2)
            p.circle(0, 0, 4).stroke("#9aa8b6", width=1)
        p.text(self.label, 8, height / 2, size=11, color="#eef2f6", align="left middle")

    def draw_layer(self, args: w.DrawArgs, layer: int) -> None:
        if layer == 1:  # Rack draws layer 1 after dimming the rack, so it stays bright
            p = Painter(args)
            p.circle(self.box.size.x - 12, self.box.size.y / 2, 3).fill("#40ff80")


def exercise_painter() -> bool:
    """Call from Rack's UI thread after its context exists, not plugin startup."""
    frame = w.FramebufferWidget()
    badge = Badge("Gate")
    frame.box = badge.box = Rect(Vec(0, 0), Vec(64, 20))
    frame.add_child(badge)
    try:
        frame.render()  # Rack calls Badge.draw (a framebuffer draws layer 0 only)
        return frame.get_framebuffer() is not None
    finally:
        frame.close()
