# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from typing import Any

from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec


class Swatch(w.OpaqueWidget):
    """Counts clicks and frames; Rack calls both overrides on its UI thread."""

    def __init__(self) -> None:
        super().__init__()
        self.clicks = 0
        self.frames = 0

    def on_button(self, event: Any) -> None:
        self.clicks += 1
        super().on_button(event)  # OpaqueWidget consumes the click

    def draw(self, args: Any) -> None:
        self.frames += 1  # args and args.vg expire when this call returns
        super().draw(args)


def exercise_widgets() -> tuple[int, int]:
    """Call from Rack's UI thread after its context exists, not plugin startup."""
    frame = w.FramebufferWidget()
    swatch = Swatch()
    frame.box = swatch.box = Rect(Vec(0, 0), Vec(30, 20))
    frame.add_child(swatch)  # the frame owns the swatch now
    try:
        click = w.ButtonEvent(pos=Vec(5, 5), button=0, action=1, context=w.EventContext())
        frame.on_button(click)  # Rack's own dispatch, by position, to the swatch
        frame.render()  # Rack's framebuffer rendering calls Swatch.draw
        assert click.get_target() is swatch
        return swatch.clicks, swatch.frames
    finally:
        frame.close()  # deletes the swatch too
