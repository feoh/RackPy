# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-33 test entry module: examples/oscillator in windowed Rack, through two launches.

Importing the example adds its model, as its own entry package does. Launch 1 opens a
patch with two oscillators, the first's sine cabled to the second's V/oct input, then
adds a third through the browser's path, deletes it and undoes that; Rack autosaves as
it exits. Launch 2 reopens that autosave.
"""

import json
import traceback
from pathlib import Path
from typing import Any

import rackpyoscillator  # noqa: F401  # adds the Oscillator model, as the example's entry package does
from _rackpy_host import _testing

from rackpy.raw.engine import Module

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "session_config.json").read_text())
RESULT: dict[str, Any] = {"sine": [], "carried": []}
FIRST, SECOND = 2, 3  # the patch's module IDs


def handle(module_id: int) -> Module:
    module = _testing.engine_handle(module_id)
    assert module is not None, f"no module {module_id}"
    return module


def sample() -> None:
    a, b = handle(FIRST), handle(SECOND)
    RESULT["sine"].append(a.outputs[0].get_voltage(0))
    RESULT["carried"].append(b.inputs[0].get_voltage(0))


def first() -> None:
    a, b = handle(FIRST), handle(SECOND)
    RESULT["connected"] = [b.inputs[0].is_connected(), b.inputs[0].get_channels()]
    RESULT["knob"] = a.params[0].value
    RESULT["preview"] = _testing.model_preview("RackPy", "Oscillator")
    added = _testing.browser_add_module("RackPy", "Oscillator")
    RESULT["added"] = [added, _testing.engine_has_module(added), _testing.module_model(added)]
    _testing.remove_module_widget(added)  # the module menu's Delete, undoable
    RESULT["after_delete"] = _testing.engine_has_module(added)
    _testing.history_undo()
    RESULT["after_undo"] = _testing.engine_has_module(added)


def second() -> None:
    a, b = handle(FIRST), handle(SECOND)
    RESULT["reopened"] = {
        "knob": a.params[0].value,
        "connected": b.inputs[0].is_connected(),
        "third": _testing.engine_has_module(CONFIG["added"]),
    }


done = [False]


def finish() -> None:
    done[0] = True
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def on_step(frame: int, _: object) -> None:
    if done[0] or frame < 5:
        return
    try:
        if frame < 40:  # Rack's engine runs between UI frames: watch the cable carry the sine
            sample()
            return
        (first if CONFIG["phase"] == 1 else second)()
    except BaseException:
        RESULT["error"] = traceback.format_exc()
    finish()


_testing.set_step_callback(on_step)
