# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::history: undo actions and Rack's history (Context.history)."""

from typing import Any, final

from rackpy.raw import app as _app
from rackpy.raw import plugin as _plugin
from rackpy.raw import widget as _widget
from rackpy.raw.componentlibrary import Template as _Template
from rackpy.raw.math import Vec
from rackpy.raw.nanovg import NVGcolor

@final
class ActionView:
    """An action Rack made, found again in its tree at every use; read-only."""

    @property
    def alive(self) -> bool: ...
    @property
    def name(self) -> str: ...
    @property
    def type_name(self) -> str: ...
    @property
    def module_id(self) -> int | None: ...
    @property
    def actions(self) -> list[Action | ActionView] | None: ...

class Action:
    """Rack's undo action. Subclass it (or ModuleAction), override undo and redo, and push it to Context.history."""

    name: str
    def __init__(self) -> None: ...
    def undo(self) -> None: ...
    def redo(self) -> None: ...
    def _rack_undo(self) -> None: ...
    def _rack_redo(self) -> None: ...
    @property
    def owned_by_rack(self) -> bool: ...

class ComplexAction(Action):
    def push(self, action: Action) -> None: ...
    def is_empty(self) -> bool: ...
    @property
    def actions(self) -> list[Action | ActionView]: ...

class ModuleAction(Action):
    module_id: int

class ModuleAdd(ModuleAction):
    model: _plugin.Model | None
    pos: Vec
    module_j: Any
    def set_module(self, module_widget: _app.ModuleWidget | _widget.HostWidget) -> None: ...

class ModuleRemove(ModuleAdd): ...

class ModuleMove(ModuleAction):
    old_pos: Vec
    new_pos: Vec

class ModuleBypass(ModuleAction):
    bypassed: bool

class ModuleChange(ModuleAction):
    old_module_j: Any
    new_module_j: Any

class ParamChange(ModuleAction):
    param_id: int
    old_value: float
    new_value: float

class CableAdd(Action):
    cable_id: int
    input_module_id: int
    input_id: int
    output_module_id: int
    output_id: int
    color: NVGcolor
    def set_cable(self, cable_widget: _app.HostCableWidget) -> None: ...
    def is_cable(self, cable_widget: _app.HostCableWidget | None) -> bool: ...

class CableRemove(CableAdd): ...

class CableColorChange(Action):
    cable_id: int
    new_color: NVGcolor
    old_color: NVGcolor
    def set_cable(self, cable_widget: _app.HostCableWidget) -> None: ...

class InverseAction(_Template):
    """Rack's InverseAction<TAction>: ``InverseAction[T]`` is T with its undo and redo swapped."""

    def undo(self) -> None: ...
    def redo(self) -> None: ...

@final
class State:
    """Rack's undo history (Context.history)."""

    @property
    def alive(self) -> bool: ...
    def push(self, action: Action) -> None: ...
    def undo(self) -> None: ...
    def redo(self) -> None: ...
    def can_undo(self) -> bool: ...
    def can_redo(self) -> bool: ...
    def get_undo_name(self) -> str: ...
    def get_redo_name(self) -> str: ...
    def set_saved(self) -> None: ...
    def is_saved(self) -> bool: ...
    @property
    def actions(self) -> list[Action | ActionView]: ...
    @property
    def action_index(self) -> int: ...
    @property
    def saved_index(self) -> int: ...
