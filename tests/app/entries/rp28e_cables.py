# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28E test entry module, in windowed Rack: Python cable widgets between two Python modules' factory ports,
through Rack's rack, engine and history. The patch has the RuntimeProbe and PyTone modules 2 and 3."""

import importlib
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
from rackpy.raw import history as h
from rackpy.raw.engine import Cable
from rackpy.raw.math import Vec

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp28e_config.json").read_text())
RESULT: dict[str, Any] = {}


def tone(m: Module) -> None:
    m.config(1, 1, 1, 0)


class ToneWidget(app.ModuleWidget):
    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.box.size = Vec(60, 380)
        self.add_input(raw.create_input_centered(componentlibrary.PJ301MPort, Vec(30, 200), module, 0))
        self.add_output(raw.create_output_centered(componentlibrary.PJ301MPort, Vec(30, 300), module, 0))


plugin.add_model("PyTone", tone, widget=ToneWidget)


def refused(action: Callable[[], object]) -> str:
    try:
        action()
    except (OwnershipError, ValueError, TypeError) as e:
        return f"{type(e).__name__}: {e}"
    return "allowed"


def run() -> None:
    context = raw.context_get()
    assert context is not None and context.scene is not None and context.engine is not None
    assert context.history is not None
    rack, engine, history = context.scene.rack, context.engine, context.history
    two, three = rack.get_module(2), rack.get_module(3)
    assert isinstance(two, ToneWidget) and isinstance(three, ToneWidget)
    out_port, in_port = two.get_output(0), three.get_input(0)
    target = three.module
    assert target is not None

    # Rack's drag flow, from Python: ports, the rack, then the engine cable.
    cw = app.CableWidget()
    cw.color = raw.nanovg.rgb(40, 120, 220)
    RESULT["plugs"] = [type(cw.input_plug).__name__, cw.input_plug.get_cable() is cw]
    RESULT["wrong_end"] = refused(lambda: setattr(cw, "input_port", out_port))
    cw.output_port, cw.input_port = out_port, in_port
    RESULT["detached_update"] = refused(cw.update_cable)
    rack.add_cable(cw)
    RESULT["in_rack"] = [cw.input_plug.parent is not None, cw.is_complete(), engine.get_num_cables()]
    cw.update_cable()
    cable = cw.cable
    RESULT["cabled"] = [cable is not None and cable.in_engine, engine.get_num_cables(), target.inputs[0].is_connected()]
    RESULT["json"] = cw.merge_json({})
    RESULT["listed"] = [rack.get_cable(cable.id) is cw if cable else False, cw in rack.get_cables()]

    # Rack's history deletes and recreates it (as Rack's own cable widget).
    add = h.CableAdd()
    add.set_cable(cw)
    history.push(add)
    history.undo()
    RESULT["undone"] = [cw.alive, engine.get_num_cables()]
    history.redo()
    redone = rack.get_cable(add.cable_id)
    RESULT["redone"] = [type(redone).__name__, engine.get_num_cables()]
    RESULT["host_remove"] = refused(lambda: rack.remove_cable(redone))  # type: ignore[arg-type]
    history.undo()

    # A cable Python made, adopted by a cable widget in the rack.
    cw2 = app.CableWidget()
    rack.add_cable(cw2)
    made = Cable()
    made.output_module, made.output_id, made.input_module, made.input_id = two.module, 0, target, 0
    cw2.set_cable(made)
    RESULT["adopted"] = [made.in_engine, cw2.input_port is in_port, cw2.is_complete(), engine.get_num_cables()]
    released = cw2.release_cable()
    RESULT["released"] = [released is not None and not released.in_engine, engine.get_num_cables(), cw2.cable is None]
    if released is not None:
        cw2.set_cable(released)
    rack.remove_cable(cw2)  # Python owns it again, without an engine cable
    RESULT["removed"] = [cw2.alive, cw2.parent is None, cw2.cable is None, engine.get_num_cables()]

    # Ports Python set on a detached widget, whose module goes meanwhile.
    cw3 = app.CableWidget()
    cw3.input_port = in_port
    three.remove_action()  # deletes the module widget and its port widgets
    RESULT["dead_port"] = cw3.input_port is None
    cw2.close()
    cw3.close()

    # The documentation's snippet: a cable with an undo step, between modules 2 and the probe's neighbour.
    cable_widget: Any = importlib.import_module("cable_widget")  # the documentation's snippet
    four = rack.get_module(4)
    assert isinstance(four, ToneWidget)
    snippet = cable_widget.connect(two.get_output(0), four.get_input(0))
    RESULT["snippet"] = [snippet.is_complete(), history.get_undo_name() == "add cable", engine.get_num_cables()]
    history.undo()
    RESULT["snippet"].append(engine.get_num_cables())


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
