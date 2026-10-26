# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24 test entry module, in windowed Rack: a Python model's modules into Rack's engine and back (RP-24A),
then cables and parameter handles (RP-24B), ending with Rack's rack and engine cleared.

The patch has the RuntimeProbe (which drives this script from its widget's
step), and two PyTone modules (IDs 2 and 3) joined by cable 7, which has
Rack's cable widget.
"""

import json
import traceback
from collections.abc import Callable
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import raw
from rackpy.engine import Module
from rackpy.errors import OwnershipError
from rackpy.plugin import plugin
from rackpy.raw import app, componentlibrary
from rackpy.raw.engine import Cable, Engine, ParamHandle
from rackpy.raw.math import Vec

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp24_config.json").read_text())
RESULT: dict[str, Any] = {}
ON_RESET: list[Callable[[], None]] = []  # what the next ResetEvent does, under Rack's exclusive lock


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def on_reset(e: object) -> None:
    if ON_RESET:
        ON_RESET.pop()()


def tone(m: Module) -> None:
    m.config(1, 1, 1, 0)
    m.config_param(0, 0, 10, 2, "Level")
    m.set_event_handlers(on_reset=on_reset)


class ToneWidget(app.ModuleWidget):
    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.box.size = Vec(60, 380)
        self.add_param(raw.create_param_centered(componentlibrary.RoundBlackKnob, Vec(30, 90), module, 0))
        self.add_input(raw.create_input_centered(componentlibrary.PJ301MPort, Vec(30, 200), module, 0))
        self.add_output(raw.create_output_centered(componentlibrary.PJ301MPort, Vec(30, 300), module, 0))


plugin.add_model("PyTone", tone, widget=ToneWidget)


def refused(action: Callable[[], object]) -> str:
    try:
        action()
    except OwnershipError as e:
        return f"refused: {e}"
    return "allowed"


def captured(action: Callable[[], object]) -> str:
    try:
        action()
    except Exception as error:
        return f"{type(error).__name__}: {error}"
    return "allowed"


def rp24a(state: dict[str, Any]) -> None:
    e = engine()
    model = raw.plugin.get_model("RackPy", "PyTone")
    assert model is not None
    m = model.create_module()  # Python owns it
    e.add_module(m)
    RESULT["added"] = [e.has_module(m), m.id >= 0, e.get_module(m.id) is m, e.module_to_json(m)["model"]]
    RESULT["again"] = refused(lambda: e.add_module(m))
    knob = raw.create_param(app.Knob, Vec(), m, 0)
    RESULT["with_control"] = refused(lambda: e.remove_module(m))
    knob.close()
    ON_RESET.append(lambda: RESULT.__setitem__("reentry", captured(e.get_module_ids)))
    e.reset_module(m)
    RESULT["reset"] = e.get_param_value(m, 0)
    master = e.get_master_module()
    e.set_master_module(m)  # Rack's own thread stops stepping
    try:
        frame = e.get_frame()
        e.step_block(64)
        e.step_block(32)
        RESULT["stepped"] = [e.get_frame() - frame, e.get_block_frames(), e.get_master_module() is m]
    finally:
        e.set_master_module(master)
    e.remove_module(m)  # Python owns it again
    RESULT["removed"] = [e.has_module(m), m.alive, e.get_module(m.id) is None]
    m.close()
    RESULT["closed"] = m.alive
    widget_id = _testing.browser_add_module("RackPy", "PyTone")
    owned = e.get_module(widget_id)
    assert isinstance(owned, Module)
    RESULT["widget_owned"] = refused(lambda: e.remove_module(owned))
    state["model"], state["owned"] = model, owned


def rp24b(state: dict[str, Any]) -> None:
    e = engine()
    model, owned = state["model"], state["owned"]
    a, b = model.create_module(), model.create_module()
    e.add_module(a)
    e.add_module(b)
    c = Cable()
    c.output_module, c.output_id, c.input_module, c.input_id = a, 0, b, 0
    e.add_cable(c)
    saved = [cable["id"] for cable in e.to_json()["cables"]]
    RESULT["cable"] = [c.in_engine, c.id >= 0, e.has_cable(c), e.get_cable(c.id) == c, c.input_module is b]
    RESULT["cable"] += [c.output_module is a, b.inputs[0].is_connected(), c.id in saved]
    RESULT["cabled_module"] = refused(lambda: e.remove_module(b))
    RESULT["cabled_widget"] = refused(lambda: model.create_module_widget(b))
    to_widget = Cable()
    to_widget.output_module, to_widget.output_id, to_widget.input_module, to_widget.input_id = a, 0, owned, 0
    RESULT["to_widget"] = refused(lambda: e.add_cable(to_widget))
    patched = e.get_cable(7)  # the patch's cable, which Rack's cable widget owns
    assert patched is not None
    input_module = patched.input_module
    RESULT["patched"] = [patched.in_engine, input_module is not None and input_module.id == 3]
    RESULT["patched"].append(refused(lambda: e.remove_cable(patched)))
    cable_id = c.id
    e.remove_cable(c)  # Python owns it again
    RESULT["cable_removed"] = [c.in_engine, e.get_cable(cable_id) is None, c.input_module is b]
    RESULT["cable_removed"] += [b.inputs[0].is_connected(), c.id == cable_id]
    e.add_cable(c)
    RESULT["cable_readded"] = [c.in_engine, c.id == cable_id]

    h = ParamHandle()
    h.text = "Python"
    e.add_param_handle(h)
    e.update_param_handle(h, owned.id, 0)
    RESULT["mapped"] = [e.get_param_handle(owned.id, 0) == h, h.module is owned]

    def no_lock() -> None:  # Rack holds the engine lock exclusively here
        master = e.get_master_module()
        found = e.get_module_no_lock(a.id) is a
        handle = e.get_param_handle_no_lock(owned.id, 0) == h
        e.update_param_handle_no_lock(h, -1, 0)  # (Rack unmaps a handle re-mapped to its own param)
        e.update_param_handle_no_lock(h, owned.id, 0)
        e.set_master_module_no_lock(a)
        moved = e.get_master_module() is a
        e.set_master_module_no_lock(master)
        RESULT["no_lock"] = [found, handle, h.module_id == owned.id, moved, e.get_master_module() is master]

    ON_RESET.append(no_lock)
    e.reset_module(a)
    state.update(a=a, cable=c, handle=h)


def clear(state: dict[str, Any]) -> None:
    """From RackPy's scene-level step, which outlives the RuntimeProbe's widget that the rack's clear deletes."""
    e = engine()
    a, c, h = state["a"], state["cable"], state["handle"]
    context = raw.context_get()
    assert context is not None and context.scene is not None
    context.scene.rack.clear()  # Rack's module widgets (and their modules and cables) go
    RESULT["rack_cleared"] = [h.in_engine, h.module is None, c.alive, a.alive]
    e.clear()  # the rack is empty now; Rack deletes what Python added, and removes the handles
    RESULT["engine_cleared"] = [h.in_engine, c.alive, a.alive, e.get_num_modules(), e.get_num_cables()]
    RESULT["remove_after_clear"] = refused(lambda: e.remove_param_handle(h))  # Rack would assert
    h.module_id = -1  # Rack's clear leaves the IDs; Rack adds only blank handles
    e.add_param_handle(h)
    readded = h.in_engine
    e.remove_param_handle(h)
    RESULT["handle_readded"] = [readded, h.in_engine]
    left = ParamHandle()
    e.add_param_handle(left)  # RackPy removes it as the context closes, before Rack deletes the engine


STATE: dict[str, Any] = {}
done = [False]


def finish() -> None:
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def after_step() -> None:
    try:
        clear(STATE)
    except BaseException:
        RESULT["error"] = traceback.format_exc()
    finally:
        finish()


def on_step(frame: int, handle: object) -> None:
    if done[0] or frame < 3:
        return
    done[0] = True
    try:
        rp24a(STATE)
        rp24b(STATE)
        _testing.call_after_ui_step(after_step)
    except BaseException:
        RESULT["error"] = traceback.format_exc()
        finish()


_testing.set_step_callback(on_step)
