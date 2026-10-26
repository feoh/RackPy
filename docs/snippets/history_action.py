# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy import raw
from rackpy.raw import history
from rackpy.raw.engine import EngineModule, Module


class LevelChange(history.ModuleAction):
    """Undoes a change of one param's value. It keeps the module's ID: Rack may delete and restore the module."""

    def __init__(self, module_id: int, param_id: int, old: float, new: float) -> None:
        super().__init__()
        self.name = "change level"
        self.module_id = module_id
        self.param_id, self.old, self.new = param_id, old, new

    def set(self, value: float) -> None:
        context = raw.context_get()
        if context is None or context.engine is None:
            return
        module = context.engine.get_module(self.module_id)
        if module is not None:
            context.engine.set_param_value(module, self.param_id, value)

    def undo(self) -> None:
        self.set(self.old)

    def redo(self) -> None:
        self.set(self.new)


def change_level(module: Module | EngineModule, param_id: int, value: float) -> None:
    """Call from Rack's UI thread: sets the param, with a step in Rack's undo history."""
    context = raw.context_get()
    assert context is not None and context.engine is not None and context.history is not None
    old = context.engine.get_param_value(module, param_id)
    action = LevelChange(module.id, param_id, old, value)
    action.redo()  # do it, then push it, as Rack does
    context.history.push(action)
