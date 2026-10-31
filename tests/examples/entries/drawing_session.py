# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-35 test entry module: the drawing and dark-panel examples over Rack's real frames.

Importing the examples adds their models, as their entry packages do. The patch has a
Lantern (2) whose output is cabled to a Meter (3). A script advances one step per UI
frame from the RuntimeProbe's step(), which runs before Rack draws; pixels are read
back in the step after the frame that drew them (Mesa's software GLX keeps the back
buffer). It records pixels, counts the Meter scale's renders, and saves screenshots.
"""

import json
import traceback
from collections.abc import Callable, Generator
from pathlib import Path
from typing import Any

import _rackpy_host
import rackpydarkpanels  # noqa: F401  # adds the examples' models, as their entry packages do
import rackpydrawing  # noqa: F401
from _rackpy_host import _testing
from rackpydrawing import meter

from rackpy import raw
from rackpy.raw import app
from rackpy.raw import settings as rack_settings
from rackpy.raw import widget as w
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import mm2px

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "drawing_config.json").read_text())
RESULT: dict[str, Any] = {}
LANTERN, METER = 2, 3
Frames = Generator[None]

# Count the scale's renders: Ticks.draw runs only when its framebuffer renders.
renders = [0]
_ticks_draw = meter.Ticks.draw


def counting_draw(self: meter.Ticks, args: w.DrawArgs) -> None:
    renders[0] += 1
    _ticks_draw(self, args)


meter.Ticks.draw = counting_draw  # type: ignore[method-assign]


def wait(frames: int) -> Frames:
    for _ in range(frames):
        yield


def until(condition: Callable[[], bool], limit: int = 30) -> Generator[None, None, int | None]:
    for waited in range(limit):
        if condition():
            return waited
        yield
    return None


def scene() -> Any:
    context = raw.context_get()
    assert context is not None and context.scene is not None
    return context.scene


def module_widget(module_id: int) -> app.ModuleWidget:
    found = scene().rack.get_module(module_id)
    assert isinstance(found, app.ModuleWidget), found
    return found


def window_point(widget: Any, x: float, y: float) -> tuple[int, int]:
    context = raw.context_get()
    assert context is not None and context.window is not None
    at = widget.get_relative_offset(Vec(x, y), scene()).mult(context.window.pixel_ratio)
    return int(at.x), int(at.y)


def pixel_mm(widget: Any, x_mm: float, y_mm: float) -> list[int]:
    """The window pixel Rack showed last frame at the widget's (x, y) in millimetres."""
    at = mm2px(Vec(x_mm, y_mm))
    return list(_testing.last_frame_pixel(*window_point(widget, at.x, at.y)))


def shot(widget: Any, name: str) -> list[int]:
    """Saves the widget's region of the last frame as raw RGBA; returns [x, y, width, height]."""
    x0, y0 = window_point(widget, 0, 0)
    x1, y1 = window_point(widget, widget.box.size.x, widget.box.size.y)
    width, height, data = _testing.last_frame_region(x0, y0, x1 - x0, y1 - y0)
    Path(CONFIG["shots"], f"{name}.rgba").write_bytes(data)
    RESULT.setdefault("shots", {})[name] = [width, height]
    return [x0, y0, width, height]


def lit(widget: Any, color: tuple[int, int, int], tolerance: int = 60) -> int:
    """How many pixels of the widget's region last frame were near color: text and bars."""
    x0, y0 = window_point(widget, 0, 0)
    x1, y1 = window_point(widget, widget.box.size.x, widget.box.size.y)
    _, _, data = _testing.last_frame_region(x0, y0, x1 - x0, y1 - y0)
    return sum(1 for i in range(0, len(data), 4) if all(abs(data[i + k] - color[k]) <= tolerance for k in range(3)))


TEXT = (0x8F, 0xD0, 0xFF)
BAR = (0x6C, 0xF2, 0x9B)


def script() -> Frames:
    prefer = rack_settings.settings.prefer_dark_panels
    rack_settings.settings.prefer_dark_panels = False
    old_brightness = _testing.set_rack_brightness(1.0, 0.0)
    yield from wait(10)
    lantern, meter_widget = module_widget(LANTERN), module_widget(METER)
    assert isinstance(meter_widget, meter.MeterWidget)
    RESULT["meter_is_python"] = True
    RESULT["placed"] = [window_point(lantern, 0, 0), window_point(meter_widget, 0, 0)]

    # Light panel, then dark: the same panel pixel, and the themed screw's.
    RESULT["light_panel"] = pixel_mm(lantern, 3.0, 80.0)
    RESULT["lamp_bright"] = pixel_mm(lantern, 15.24, 62.0)
    RESULT["text_pixels"] = lit(meter_widget.readout, TEXT)
    RESULT["bar_pixels"] = lit(meter_widget.bar, BAR)
    RESULT["shapes_pixels"] = {
        "panel": pixel_mm(meter_widget, 30.48, 20.0),
        "circle": pixel_mm(meter_widget, 5.0 + 50.96 * 0.25, 33.0),
        "triangle_edge": pixel_mm(meter_widget, 5.0 + 50.96 * 0.7, 18.0 + 30.0 * 0.2 + 0.5),
    }
    shot(lantern, "lantern-light")
    shot(meter_widget, "meter")

    # A room at 25%: the panel layer dims, the lights and the self-lit bar do not.
    _testing.set_rack_brightness(0.25, 0.0)
    yield from wait(4)
    RESULT["dim_panel"] = pixel_mm(lantern, 3.0, 80.0)
    RESULT["dim_lamp"] = pixel_mm(lantern, 15.24, 62.0)
    RESULT["dim_shapes_circle"] = pixel_mm(meter_widget, 5.0 + 50.96 * 0.25, 33.0)
    RESULT["dim_bar_pixels"] = lit(meter_widget.bar, BAR)
    # Dark panels in the dark room, then in full light.
    rack_settings.settings.prefer_dark_panels = True
    yield from wait(4)
    shot(lantern, "lantern-dark-room")
    shot(meter_widget, "meter-dark-room")
    _testing.set_rack_brightness(1.0, 0.0)
    yield from wait(4)
    RESULT["dark_panel"] = pixel_mm(lantern, 3.0, 80.0)
    shot(lantern, "lantern-dark")
    rack_settings.settings.prefer_dark_panels = False
    yield from wait(4)

    # The cached scale: rendered once, not again until RANGE changes.
    first = renders[0]
    yield from wait(10)
    RESULT["idle_renders"] = renders[0] - first
    module = meter_widget.module
    assert module is not None
    module.params[meter.Param.RANGE].value = 0  # 10 V to 5 V
    RESULT["range_render_frames"] = yield from until(lambda: renders[0] > first)
    yield from wait(10)
    RESULT["range_renders"] = renders[0] - first

    # A missing font: the readout draws nothing, and nothing raises.
    raised = _rackpy_host.dispatch_stats()["raised"]
    font = meter.FONT
    meter.FONT = "res/fonts/RackPyMissingFont.ttf"
    yield from wait(4)
    RESULT["missing_font_text_pixels"] = lit(meter_widget.readout, TEXT)
    meter.FONT = font
    yield from wait(4)
    RESULT["restored_text_pixels"] = lit(meter_widget.readout, TEXT)
    RESULT["raised_during_missing_font"] = _rackpy_host.dispatch_stats()["raised"] - raised

    # A new graphics context, as when a host recreates its window: fonts load again, the cache renders again.
    before = renders[0]
    _testing.recreate_graphics_context()
    yield from wait(6)
    RESULT["recreated"] = {"renders": renders[0] - before, "text_pixels": lit(meter_widget.readout, TEXT)}

    # Deleting the Meter while it draws, then undoing: nothing crashes, and it draws again.
    _testing.remove_module_widget(METER)
    yield from wait(4)
    RESULT["deleted"] = scene().rack.get_module(METER) is None
    _testing.history_undo()
    yield from wait(6)
    restored = module_widget(METER)
    assert isinstance(restored, meter.MeterWidget)
    RESULT["undone_text_pixels"] = lit(restored.readout, TEXT)

    # The browser's preview: a widget with no module, rendered off-screen.
    model = raw.plugin.get_model("RackPy", "Meter")
    assert model is not None
    preview = model.create_module_widget(None)
    assert isinstance(preview, meter.MeterWidget)
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), preview.box.size)
    frame.add_child(preview)
    frame.render()
    width, height, data = _testing.framebuffer_pixels(frame)
    Path(CONFIG["shots"], "meter-preview.rgba").write_bytes(data)
    RESULT.setdefault("shots", {})["meter-preview"] = [width, height]
    frame.close()
    RESULT["preview"] = [width, height]

    rack_settings.settings.prefer_dark_panels = prefer
    _testing.set_rack_brightness(*old_brightness)
    RESULT["raised_total"] = _rackpy_host.dispatch_stats()["raised"]


_running: list[Frames] = []


def finish() -> None:
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def step(frame: int, handle: Any) -> None:
    if frame < 3 or RESULT.get("done"):
        return
    try:
        if not _running:
            _running.append(script())
        next(_running[0])
    except StopIteration:
        RESULT["done"] = True
        finish()
    except Exception:
        RESULT["error"] = traceback.format_exc()
        RESULT["done"] = True
        finish()


_testing.set_step_callback(step)
