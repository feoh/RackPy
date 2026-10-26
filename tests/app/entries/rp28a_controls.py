# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28A test entry module: Python controls inside a Python model's ModuleWidget, through Rack's own paths."""

import json
import traceback
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import raw
from rackpy.engine import Module
from rackpy.plugin import plugin
from rackpy.raw import app
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.math import Vec

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp28a_config.json").read_text())
RESULT: dict[str, Any] = {}
LEFT, PRESS, RELEASE = 0, 1, 0


def jacks(m: Module) -> None:
    m.config(1, 1, 1, 1)
    m.config_param(0, 0, 10, 5, "Gain")
    m.config_input(0, "In")
    m.config_output(0, "Out")
    m.config_light(0, "Level")


plugin.add_model("PyJacks", jacks, hp=6)


class GreenLight(app.ModuleLightWidget):
    def __init__(self) -> None:
        super().__init__()
        self.add_base_color(nvg.rgb(0, 255, 0))  # as Rack's light components do in their constructors


def centre(widget: w.Widget) -> Vec:
    context = raw.context_get()
    assert context is not None and context.event is not None and context.event.root_widget is not None
    return widget.get_relative_offset(widget.box.size.div(2), context.event.root_widget)


def controls(module_id: int) -> list[w.Widget]:
    module = _testing.engine_handle(module_id)
    assert module is not None
    knob = raw.create_param(app.Knob, Vec(10, 40), module, 0)
    knob.box.size = Vec(30, 30)
    out = raw.create_output(app.PortWidget, Vec(10, 100), module, 0)
    inp = raw.create_input(app.PortWidget, Vec(10, 160), module, 0)
    for port in (out, inp):
        port.box.size = Vec(24, 24)
    light = raw.create_light(GreenLight, Vec(10, 220), module, 0)
    made: list[w.Widget] = [knob, out, inp, light]
    for widget in made:
        _testing.module_widget_add_child_by_id(module_id, widget)  # as a ModuleWidget adds its controls
    return made


def run() -> None:
    events = raw.context_get().event  # type: ignore[union-attr]
    assert events is not None
    removed_id = _testing.browser_add_module("RackPy", "PyJacks")
    knob, out, inp, light = controls(removed_id)
    kept_id = _testing.browser_add_module("RackPy", "PyJacks")
    controls(kept_id)  # left in place: Rack deletes them when it quits

    at = centre(knob)
    events.handle_button(at, LEFT, PRESS, 0)
    events.handle_button(at, LEFT, RELEASE, 0)
    RESULT["touched_is_knob"] = _testing.touched_param() is knob
    before = _testing.rack_cables()
    events.handle_button(centre(out), LEFT, PRESS, 0)
    events.handle_hover(centre(inp), centre(inp).minus(centre(out)))
    events.handle_button(centre(inp), LEFT, RELEASE, 0)
    RESULT["cables"] = [list(before), list(_testing.rack_cables())]

    _testing.remove_module_widget(removed_id)  # the module menu's Delete, undoable
    RESULT["after_remove"] = {
        "controls_alive": [widget.alive for widget in (knob, out, inp, light)],
        "touched": _testing.touched_param() is None,
        "module": _testing.engine_has_module(removed_id),
    }
    _testing.history_undo()  # Rack recreates the module and its own widget
    RESULT["after_undo"] = _testing.engine_has_module(removed_id)


done = [False]


def on_step(frame: int, handle: object) -> None:
    if done[0] or frame < 3:
        return
    done[0] = True
    try:
        run()
    except BaseException:
        RESULT["error"] = traceback.format_exc()
    finally:
        Path(CONFIG["result"]).write_text(json.dumps(RESULT))
        _testing.close_window()


_testing.set_step_callback(on_step)
