# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's running engine and undo history, Pythonically, over :mod:`rackpy.raw.engine` and
:mod:`rackpy.raw.history`. Inside Rack's live UI only.

Everything here is synchronous: each call runs Rack's own code on Rack's main
thread before it returns, and nothing is queued. Calls that take Rack's
engine lock release the GIL while they wait. A call that fails leaves the
engine as it was: the raw layer's checks run before Rack's code, and
:func:`map_param` and :func:`undo_step` undo what they did before raising.
The raw layer's rules apply unchanged (see the engine and history reference
pages); nothing here has native semantics of its own.
"""

from collections.abc import Iterator
from contextlib import contextmanager

from rackpy.errors import ContextError
from rackpy.raw import context_get
from rackpy.raw.engine import Cable, Engine, EngineModule, Module, ParamHandle
from rackpy.raw.history import ComplexAction, State
from rackpy.raw.nanovg import NVGcolor

__all__ = [
    "AnyModule",
    "cables",
    "connect",
    "disconnect",
    "engine",
    "history",
    "map_param",
    "module",
    "modules",
    "undo_step",
    "unmap_param",
]

AnyModule = Module | EngineModule
"""A RackPy module's handle, or another plugin's module."""


def engine() -> Engine:
    """Rack's engine (``Context.engine``). ContextError outside Rack's live UI."""
    context = context_get()
    if context is None or context.engine is None:
        raise ContextError("Rack's engine is available only in Rack's live UI context")
    return context.engine


def history() -> State:
    """Rack's undo history (``Context.history``). ContextError outside Rack's live UI."""
    context = context_get()
    if context is None or context.history is None:
        raise ContextError("Rack's history is available only in Rack's live UI context")
    return context.history


def modules() -> list[AnyModule]:
    """The engine's modules, in Rack's order. A module Rack deletes meanwhile is left out."""
    e = engine()
    found = (e.get_module(module_id) for module_id in e.get_module_ids())
    return [m for m in found if m is not None]


def module(module_id: int) -> AnyModule | None:
    """The engine's module with this ID, or None."""
    return engine().get_module(module_id)


def cables() -> list[Cable]:
    """The engine's cables (cable widgets' and Python's), each found again by its ID at every use."""
    e = engine()
    found = (e.get_cable(cable_id) for cable_id in e.get_cable_ids())
    return [c for c in found if c is not None]


def connect(output: tuple[Module, int], input: tuple[Module, int]) -> Cable:
    """Connect ``(module, output_id)`` to ``(module, input_id)`` with a cable in Rack's engine.

    As :meth:`Engine.add_cable <rackpy.raw.engine.Engine.add_cable>`: both modules must be RackPy modules
    that Python put into the engine and that no module widget owns, since a cable added here has no cable
    widget. Returns the cable, which Rack's engine then owns.
    """
    cable = Cable()
    cable.output_module, cable.output_id = output
    cable.input_module, cable.input_id = input
    engine().add_cable(cable)
    return cable


def disconnect(cable: Cable) -> None:
    """Remove a cable Python connected (no cable widget owns it) from Rack's engine."""
    engine().remove_cable(cable)


def map_param(
    target: AnyModule,
    param_id: int,
    *,
    text: str = "",
    color: NVGcolor | None = None,
    overwrite: bool = True,
) -> ParamHandle:
    """Map a new parameter handle to one module's param, as Core's MIDI-Map does, and return it.

    With ``overwrite``, a handle already on that param is unmapped; without, this one ends up unmapped
    (Rack's rule), and the returned handle's ``module_id`` is -1. If mapping fails, the handle leaves the
    engine again before the error propagates.
    """
    e = engine()
    handle = ParamHandle()
    handle.text = text
    if color is not None:
        handle.color = color
    e.add_param_handle(handle)
    try:
        e.update_param_handle(handle, target.id, param_id, overwrite)
    except BaseException:
        e.remove_param_handle(handle)
        raise
    return handle


def unmap_param(handle: ParamHandle) -> None:
    """Remove a handle :func:`map_param` made from Rack's engine (refused once Rack's clear removed it)."""
    engine().remove_param_handle(handle)


@contextmanager
def undo_step(name: str) -> Iterator[ComplexAction]:
    """Collect actions into one undo step: push each change's action to the yielded ComplexAction.

    On a normal exit the step goes to Rack's history, unless it is empty. If the block raises, the
    collected actions are undone (Rack's code, newest first) and the step is dropped, so the patch is as
    it was before the block. Make each change, then push its action, as Rack does.
    """
    step = ComplexAction()
    step.name = name
    try:
        yield step
    except BaseException:
        step.undo()
        raise
    if not step.is_empty():
        history().push(step)
