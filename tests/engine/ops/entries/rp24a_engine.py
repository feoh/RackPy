# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24A test entry module: a Python model's modules into Rack's engine and back, in windowed Rack."""

import json
import traceback
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import raw
from rackpy.engine import Module
from rackpy.errors import OwnershipError
from rackpy.plugin import plugin
from rackpy.raw import app
from rackpy.raw.math import Vec

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp24a_config.json").read_text())
RESULT: dict[str, Any] = {}


def reenter(e: object) -> None:  # Rack holds the engine lock exclusively in a ResetEvent
    try:
        context = raw.context_get()
        assert context is not None and context.engine is not None
        context.engine.get_module_ids()
        RESULT["reentry"] = "allowed"
    except Exception as error:
        RESULT["reentry"] = f"{type(error).__name__}: {error}"


def tone(m: Module) -> None:
    m.config(1, 0, 1, 0)
    m.config_param(0, 0, 10, 2, "Level")
    m.set_event_handlers(on_reset=reenter)


plugin.add_model("PyTone", tone)


def refused(action: Any) -> str:
    try:
        action()
    except OwnershipError as e:
        return f"refused: {e}"
    return "allowed"


def run() -> None:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    engine = context.engine
    model = raw.plugin.get_model("RackPy", "PyTone")
    assert model is not None
    m = model.create_module()  # Python owns it
    engine.add_module(m)
    RESULT["added"] = [engine.has_module(m), m.id >= 0, engine.get_module(m.id) is m, engine.module_to_json(m)["model"]]
    RESULT["again"] = refused(lambda: engine.add_module(m))
    knob = raw.create_param(app.Knob, Vec(), m, 0)
    RESULT["with_control"] = refused(lambda: engine.remove_module(m))
    knob.close()
    engine.reset_module(m)
    RESULT["reset"] = engine.get_param_value(m, 0)
    master = engine.get_master_module()
    engine.set_master_module(m)  # Rack's own thread stops stepping
    try:
        frame = engine.get_frame()
        engine.step_block(64)
        engine.step_block(32)
        RESULT["stepped"] = [engine.get_frame() - frame, engine.get_block_frames(), engine.get_master_module() is m]
    finally:
        engine.set_master_module(master)
    engine.remove_module(m)  # Python owns it again
    RESULT["removed"] = [engine.has_module(m), m.alive, engine.get_module(m.id) is None]
    m.close()
    RESULT["closed"] = m.alive
    widget_id = _testing.browser_add_module("RackPy", "PyTone")
    owned = engine.get_module(widget_id)
    RESULT["widget_owned"] = refused(lambda: engine.remove_module(owned))  # type: ignore[arg-type]


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
