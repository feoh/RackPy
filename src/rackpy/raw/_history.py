# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::history's class template InverseAction, as a Python template class (RP-24C).

``InverseAction[T]`` is a cached subclass of ``T`` whose undo runs ``T``'s redo and whose redo runs
``T``'s undo, as Rack's ``InverseAction<TAction>`` does. For Rack's own action types that is Rack's
code, checked (``ModuleRemove`` is ``InverseAction<ModuleAdd>`` in Rack); for a Python action it is
the Python method. This module adds it to the native ``rackpy.raw.history``.
"""

from typing import Any

import _rackpy_host

from rackpy.raw._componentlibrary import Template

_lib: Any = _rackpy_host.raw.history
_MODULE = "rackpy.raw.history"


def _run(action_type: type, name: str, action: object) -> None:
    for klass in action_type.__mro__:
        if f"_rack_{name}" in vars(klass):  # a native action type: Rack's own code, never an override
            getattr(klass, f"_rack_{name}")(action)
            return
        if name in vars(klass):  # a Python action's own method
            vars(klass)[name](action)
            return
    raise TypeError(f"{action_type.__name__} is not a history action")


class InverseAction(Template):
    """Rack's ``InverseAction<TAction>``: ``InverseAction[T]`` swaps ``T``'s undo and redo."""

    _params = (None,)
    _args_InverseAction: tuple[type, ...]

    @classmethod
    def _bases(cls, *args: type) -> tuple[type, ...]:
        if not (isinstance(args[0], type) and issubclass(args[0], _lib.Action)):
            raise TypeError("InverseAction's template argument is a rackpy.raw.history.Action class")
        return (args[0],)

    def undo(self) -> None:
        _run(self._args_InverseAction[0], "redo", self)

    def redo(self) -> None:
        _run(self._args_InverseAction[0], "undo", self)


InverseAction.__module__ = _MODULE
_lib.InverseAction = InverseAction
