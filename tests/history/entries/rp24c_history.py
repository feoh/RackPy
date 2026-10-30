# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24C test entry module, in windowed Rack: history actions against Rack's real rack.

The patch has the RuntimeProbe (which drives this script) and two PyTone
modules with Python module widgets (IDs 2 and 3) joined by cable 7.
"""

import json
import traceback
from pathlib import Path
from typing import Any

from _rackpy_host import _testing

from rackpy import raw
from rackpy.engine import Module
from rackpy.plugin import plugin
from rackpy.raw import app, componentlibrary
from rackpy.raw import history as h
from rackpy.raw.math import Vec

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "rp24c_config.json").read_text())
RESULT: dict[str, Any] = {}


def tone(m: Module) -> None:
    m.config(1, 1, 1, 0)
    m.config_param(0, 0, 10, 2, "Level")


class ToneWidget(app.ModuleWidget):
    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.box.size = Vec(60, 380)
        self.add_param(raw.create_param_centered(componentlibrary.RoundBlackKnob, Vec(30, 90), module, 0))
        self.add_input(raw.create_input_centered(componentlibrary.PJ301MPort, Vec(30, 200), module, 0))
        self.add_output(raw.create_output_centered(componentlibrary.PJ301MPort, Vec(30, 300), module, 0))


plugin.add_model("PyTone", tone, widget=ToneWidget)


def parts() -> tuple[Any, Any, h.State]:
    context = raw.context_get()
    assert context is not None and context.scene is not None and context.engine is not None
    assert context.history is not None
    return context.scene.rack, context.engine, context.history


def cables() -> int:
    rack, engine, _ = parts()
    return int(len(rack.get_cables()) * 10 + engine.get_num_cables())


def run() -> None:
    rack, engine, history = parts()
    two, three = rack.get_module(2), rack.get_module(3)
    assert isinstance(two, ToneWidget) and isinstance(three, ToneWidget)
    module = two.module
    assert module is not None
    RESULT["start_cables"] = cables()

    # Rack's own actions are ActionViews.
    two.bypass_action(True)
    view = history.actions[-1]
    RESULT["bypass"] = [
        isinstance(view, h.ActionView),
        getattr(view, "type_name", ""),
        getattr(view, "module_id", None),
    ]
    history.undo()
    RESULT["bypass"] += [module.is_bypassed(), getattr(view, "alive", False)]
    history.redo()
    RESULT["bypass"].append(module.is_bypassed())
    history.undo()

    three.disconnect_action()  # Rack's ComplexAction of CableRemoves
    batch = history.actions[-1]
    children = getattr(batch, "actions", None) or []
    RESULT["disconnect"] = [cables(), getattr(batch, "type_name", ""), [getattr(c, "type_name", "") for c in children]]
    history.undo()
    RESULT["disconnect"].append(cables())

    # Rack fills a ComplexAction Python owns.
    gathered = h.ComplexAction()
    three.append_disconnect_actions(gathered)
    RESULT["append"] = [len(gathered.actions), getattr(gathered.actions[0], "type_name", ""), cables()]
    history.push(gathered)  # as Rack's disconnect_action does
    history.undo()
    RESULT["append"] += [cables(), gathered.owned_by_rack]

    # Python's own actions against the real cable.
    cable_widget = rack.get_cable(7)
    change = h.CableColorChange()
    change.set_cable(cable_widget)
    original_color = cable_widget.color
    change.old_color = original_color
    change.new_color = raw.nanovg.rgb(37, 83, 129)
    change.redo()
    changed = cable_widget.color
    change.undo()
    restored = cable_widget.color
    RESULT["cable_color"] = [
        change.cable_id == 7,
        abs(change.new_color.r - 37 / 255) < 1e-5,
        abs(change.old_color.g - original_color.g) < 1e-5,
        abs(changed.r - 37 / 255) < 1e-5,
        abs(restored.r - original_color.r) < 1e-5,
    ]
    add = h.CableAdd()
    add.set_cable(cable_widget)
    RESULT["cable_add"] = [add.cable_id, add.output_module_id, add.input_module_id, add.is_cable(cable_widget)]
    remove = h.CableRemove()
    remove.set_cable(cable_widget)
    remove.redo()  # Rack's CableAdd undo: the cable widget goes
    RESULT["cable_remove"] = [cables(), rack.get_cable(7) is None]
    remove.undo()  # Rack's CableAdd redo: the cable and its widget come back
    RESULT["cable_remove"] += [cables(), rack.get_cable(7) is not None]
    bad = h.CableAdd()
    bad.output_module_id, bad.output_id, bad.input_module_id, bad.input_id = 2, 0, 3, 4  # no input 4
    bad.redo()
    RESULT["bad_cable"] = cables()

    # Rack's module drag action, pushed to Rack's history.
    rack.update_module_old_positions()
    old = Vec(two.box.pos.x, two.box.pos.y)
    rack.set_module_pos_force(two, Vec(old.x + 300, old.y))
    drag = rack.get_module_drag_action()
    names = [getattr(a, "type_name", "") for a in drag.actions]
    history.push(drag)
    history.undo()
    RESULT["drag"] = [names, drag.owned_by_rack, two.box.pos.x == old.x]
    history.redo()
    RESULT["drag"].append(two.box.pos.x == old.x + 300)

    # Python's ModuleMove, and ModuleRemove deleting and recreating a module through its Python factory.
    move = h.ModuleMove()
    move.module_id, move.old_pos, move.new_pos = 2, Vec(old.x + 300, old.y), old
    move.redo()
    RESULT["move"] = two.box.pos.x == old.x
    removal = h.ModuleRemove()
    removal.set_module(three)
    RESULT["removal"] = [removal.model is not None and removal.model.slug == "PyTone", removal.module_id == 3]
    RESULT["removal"].append(isinstance(removal.module_j, dict))
    removal.redo()  # Rack's ModuleAdd undo: the widget and module go
    RESULT["removal"].append(rack.get_module(3) is None)
    removal.undo()  # Rack's ModuleAdd redo: the module and its Python widget come back
    back = rack.get_module(3)
    RESULT["removal"] += [isinstance(back, ToneWidget), engine.get_module(3) is not None]


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
