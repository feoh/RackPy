# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24 test entry module: rackpy.engine.host in windowed Rack, with a Python model and the RuntimeProbe."""

import json
import traceback
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import raw
from rackpy.engine import Module, host
from rackpy.errors import ContextError, OwnershipError
from rackpy.plugin import plugin
from rackpy.raw import history as h
from rackpy.raw.engine import EngineModule

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "host_config.json").read_text())
RESULT: dict[str, Any] = {}


def tone(m: Module) -> None:
    m.config(1, 1, 1, 0)
    m.config_param(0, 0, 10, 2, "Level")


plugin.add_model("PyTone", tone)

try:
    host.engine()  # plugin start-up: Rack has no context yet
    RESULT["startup"] = "allowed"
except ContextError:
    RESULT["startup"] = "refused"


def run() -> None:
    e = host.engine()
    probe = next(m for m in host.modules() if isinstance(m, EngineModule))
    RESULT["modules"] = [host.module(probe.id) == probe, host.module(-9) is None]
    model = raw.plugin.get_model("RackPy", "PyTone")
    assert model is not None
    a, b = model.create_module(), model.create_module()
    assert isinstance(a, Module) and isinstance(b, Module)
    e.add_module(a)
    e.add_module(b)
    cable = host.connect((a, 0), (b, 0))
    RESULT["connect"] = [cable in host.cables(), b.inputs[0].is_connected()]
    try:
        host.connect((a, 0), (probe, 0))  # type: ignore[arg-type]
        RESULT["connect_refused"] = "allowed"
    except OwnershipError as error:
        RESULT["connect_refused"] = str(error)
    host.disconnect(cable)
    RESULT["disconnect"] = [cable in host.cables(), b.inputs[0].is_connected()]

    handle = host.map_param(a, 0, text="Level", color=raw.nanovg.rgb(0, 200, 120))
    RESULT["map"] = [e.get_param_handle(a.id, 0) == handle, handle.text]
    try:
        host.map_param(a, 5)
        RESULT["map_rollback"] = "allowed"
    except ValueError:
        # the failed handle left the engine again: only the first is mapped
        RESULT["map_rollback"] = e.get_param_handle(a.id, 0) == handle
    host.unmap_param(handle)
    RESULT["unmap"] = e.get_param_handle(a.id, 0) is None

    start = host.history().action_index
    with host.undo_step("set levels") as step:
        for m in (a, b):
            change = h.ParamChange()
            change.module_id, change.param_id = m.id, 0
            change.old_value, change.new_value = e.get_param_value(m, 0), 7.0
            change.redo()
            step.push(change)
    RESULT["step"] = [host.history().action_index == start + 1, host.history().get_undo_name(), e.get_param_value(b, 0)]
    host.history().undo()
    RESULT["step"].append(e.get_param_value(b, 0))
    try:
        with host.undo_step("fails") as step:
            change = h.ParamChange()
            change.module_id, change.param_id, change.old_value, change.new_value = a.id, 0, 2.0, 9.0
            change.redo()
            step.push(change)
            e.set_param_value(b, 0, 5.0)  # no history action records this change
            raise RuntimeError("partway")
    except RuntimeError:
        # undone, and nothing pushed: a push would have dropped the redo of "set levels"
        state = host.history()
        RESULT["rollback"] = [e.get_param_value(a, 0), state.action_index == start, state.get_redo_name()]
        RESULT["unrecorded"] = e.get_param_value(b, 0)


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
