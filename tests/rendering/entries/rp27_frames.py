# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-27B test entry module: framebuffers and Rack's self-lit layer over Rack's real frames.

A script advances one step per frame from the RuntimeProbe's step(), which
runs before Rack draws the frame. Each window pixel is read back in the step
after the frame that drew it (from the back buffer, which Mesa's software
GLX keeps after the swap).
"""

import json
import traceback
from collections.abc import Callable, Generator
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import raw
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp27_config.json").read_text())
RESULT: dict[str, Any] = {}
RED, GREEN = nvg.rgb(255, 0, 0), nvg.rgb(0, 255, 0)
Frames = Generator[None]


def fill(vg: w.GraphicsContext, x: float, y: float, width: float, height: float, color: Any) -> None:
    nvg.begin_path(vg)
    nvg.rect(vg, x, y, width, height)
    nvg.fill_color(vg, color)
    nvg.fill(vg)


class Counter(w.Widget):
    """Counts how often Rack draws it, which in a framebuffer means how often the framebuffer renders."""

    def __init__(self) -> None:
        super().__init__()
        self.draws = 0

    def draw(self, args: Any) -> None:
        self.draws += 1
        fill(args.vg, 0, 0, self.box.size.x, self.box.size.y, GREEN)


class Lit(w.Widget):
    """Red on the panel layer (left half) and on Rack's self-lit layer 1 (right half)."""

    def __init__(self) -> None:
        super().__init__()
        self.layers: set[int] = set()

    def draw(self, args: Any) -> None:
        self.layers.add(0)
        fill(args.vg, 0, 0, self.box.size.x / 2, self.box.size.y, RED)

    def draw_layer(self, args: Any, layer: int) -> None:
        self.layers.add(layer)
        if layer == 1:
            fill(args.vg, self.box.size.x / 2, 0, self.box.size.x / 2, self.box.size.y, RED)


def wait(frames: int) -> Frames:
    for _ in range(frames):
        yield


def until(condition: Callable[[], bool], limit: int = 30) -> Generator[None, None, int | None]:
    """Frames waited until condition() holds, or None after limit frames."""
    for waited in range(limit):
        if condition():
            return waited
        yield
    return None


def pixel(widget: w.Widget, x: float, y: float) -> tuple[int, int, int, int]:
    """The window pixel Rack showed last frame at the widget's own coordinates (x, y)."""
    context = raw.context_get()
    assert context is not None and context.window is not None and context.event is not None
    scene = context.event.root_widget
    assert scene is not None
    at = widget.get_relative_offset(Vec(x, y), scene).mult(context.window.pixel_ratio)
    return _testing.last_frame_pixel(int(at.x), int(at.y))


def script(handle: Any) -> Frames:
    counter = Counter()
    counter.box = Rect(Vec(0, 0), Vec(24, 24))
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(200.0, 120.0), Vec(24, 24))
    frame.add_child(counter)
    frame.attach_to_scene()
    lit = Lit()
    lit.box = Rect(Vec(4, 120), Vec(20, 20))
    _testing.module_widget_add_child(handle, lit)
    RESULT["old_brightness"] = _testing.set_rack_brightness(0.25, 0.0)

    RESULT["first_render_frames"] = yield from until(lambda: counter.draws >= 1)
    yield from wait(5)  # clean: Rack keeps drawing the framebuffer's image without rendering it again
    RESULT["clean"] = [counter.draws, bool(frame.dirty)]

    frame.set_dirty()
    renders = counter.draws
    RESULT["dirty_render_frames"] = yield from until(lambda: counter.draws > renders)
    RESULT["after_dirty"] = counter.draws - renders

    renders = counter.draws
    frame.box.pos.x += 0.5  # a new subpixel offset: Rack renders again
    RESULT["subpixel_render_frames"] = yield from until(lambda: counter.draws > renders)
    frame.dirty_on_subpixel_change = False
    yield from wait(2)
    renders = counter.draws
    frame.box.pos.x += 0.5
    yield from wait(5)
    RESULT["subpixel_ignored"] = counter.draws - renders

    frame.bypassed = True  # drawn directly every frame
    renders = counter.draws
    yield from wait(5)
    RESULT["bypassed_draws"] = counter.draws - renders
    frame.bypassed = False

    yield from wait(2)
    RESULT["framebuffer_pixel"] = pixel(counter, 12, 12)
    RESULT["layers"] = sorted(lit.layers)
    RESULT["panel_pixel"] = pixel(lit, 5, 10)  # drawn before RackWidget dims the rack
    RESULT["lit_pixel"] = pixel(lit, 15, 10)  # layer 1: drawn after the dimming
    _testing.set_rack_brightness(0.5, 0.0)
    yield from wait(2)
    RESULT["half_panel_pixel"] = pixel(lit, 5, 10)
    _testing.set_rack_brightness(*RESULT["old_brightness"])
    yield from wait(2)
    RESULT["undimmed_panel_pixel"] = pixel(lit, 5, 10)
    frame.remove_from_parent()
    frame.close()


_running: list[Frames] = []


def finish() -> None:
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def step(frame: int, handle: Any) -> None:
    if frame < 3 or RESULT.get("done"):
        return
    try:
        if not _running:
            _running.append(script(handle))
        next(_running[0])
    except StopIteration:
        RESULT["done"] = True
        finish()
    except Exception:
        RESULT["error"] = traceback.format_exc()
        RESULT["done"] = True
        finish()


_testing.set_step_callback(step)
