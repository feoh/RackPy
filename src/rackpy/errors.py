# SPDX-License-Identifier: GPL-3.0-or-later
"""Exceptions that RackPy raises.

Every RackPy error derives from :class:`RackPyError`, and also from the
built-in exception a Python programmer would expect, so ``except
ReferenceError`` and ``except RackPyError`` both work.

The native runtime raises these (``native/runtime/errors.hpp`` maps one C++
type to each). Python never sees a C++ exception, and no exception from Python
code reaches Rack: callbacks that raise are caught at the boundary, logged, and
recorded in :func:`rackpy.runtime.diagnostics`.
"""

__all__ = [
    "ContextError",
    "EngineLockError",
    "OwnershipError",
    "RackPyError",
    "StaleHandleError",
    "ThreadError",
]


class RackPyError(Exception):
    """Base class of every exception RackPy raises itself."""


class StaleHandleError(RackPyError, ReferenceError):
    """A handle to something Rack has deleted, or to a Rack context that has closed.

    Python holds handles to Rack-owned objects, never the objects. When Rack
    deletes the object, or the Rack context shuts down, every handle to it
    raises this on use, instead of touching freed memory. A new object at the
    same address gets a new handle; the old one stays stale.
    """


class ContextError(RackPyError, RuntimeError):
    """A handle used with a Rack context it does not belong to.

    RackPy binds to the first Rack context it sees and refuses any other in
    the same process. Hosts that run several Rack instances in one process,
    such as Rack Pro inside a DAW, are not supported.
    """


class ThreadError(RackPyError, RuntimeError):
    """An operation called on a thread that may not perform it.

    Most of the Rack API is for Rack's main (UI) thread only. Calling it from a
    thread you started raises this rather than racing with Rack.
    """


class OwnershipError(RackPyError, ValueError):
    """An ownership transfer that cannot happen.

    For example, handing an object you created to Rack twice: after the first
    transfer Rack owns it, and only Rack may delete it.
    """


class EngineLockError(RackPyError, RuntimeError):
    """A call that takes Rack's engine lock, made where Rack already holds it exclusively.

    Rack runs some callbacks, such as a module's reset, with audio stopped and
    the engine locked. The engine lock is not recursive, so calling a locking
    engine function there would deadlock Rack; RackPy raises this instead.
    """
