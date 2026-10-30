# SPDX-License-Identifier: GPL-3.0-or-later
"""The embedded Python runtime: threads, shutdown hooks and diagnostics.

Available only inside Rack, in a plugin built with RackPy. Importing it
anywhere else raises :class:`ImportError`.
"""

from collections.abc import Callable
from dataclasses import dataclass
from typing import Literal, cast

try:
    import _rackpy_host
except ImportError as e:
    raise ImportError("rackpy.runtime is available only inside Rack, in a RackPy plugin") from e

__all__ = ["Diagnostic", "ThreadRole", "diagnostics", "is_main_thread", "on_context_closing", "thread_role"]

ThreadRole = Literal["main", "rack", "other"]
"""``main``: Rack's main (UI) thread. ``rack``: one of Rack's engine or driver threads, where
Python never runs. ``other``: a thread without a Rack context, such as one you started."""

_ROLES = ("main", "rack", "other")
_closing_callbacks: list[Callable[[], None]] = []


def _role(name: str) -> ThreadRole:
    if name not in _ROLES:
        raise ValueError(f"unknown thread role {name!r} from the host")
    return cast(ThreadRole, name)  # pyright: ignore[reportUnnecessaryCast]


@dataclass(frozen=True)
class Diagnostic:
    """One event the runtime recorded instead of interrupting Rack."""

    sequence: int
    """1, 2, 3, ... in recording order. Pass the last one you saw to :func:`diagnostics` to get only newer ones."""
    time: float
    """Seconds on Rack's clock when it was recorded."""
    code: str
    """What happened, for example ``callback_raised`` or ``refused_rack_thread``."""
    thread: ThreadRole
    """The thread it happened on."""
    message: str
    """Detail, truncated to 159 bytes."""


def diagnostics(after: int = 0) -> list[Diagnostic]:
    """The recorded diagnostics newer than sequence number ``after``, oldest first.

    The runtime keeps the most recent 256; older ones are overwritten. Recording
    one never blocks, allocates or calls Python, so it is safe even on the
    audio path, which is why these are collected rather than raised.
    """
    return [
        Diagnostic(sequence, time, code, _role(role), message)
        for sequence, time, code, role, message in _rackpy_host.diagnostics(after)
    ]


def thread_role() -> ThreadRole:
    """Which kind of thread the caller is on."""
    return _role(_rackpy_host.thread_role())


def is_main_thread() -> bool:
    """True on Rack's main (UI) thread."""
    return thread_role() == "main"


def on_context_closing(callback: Callable[[], None]) -> Callable[[], None]:
    """Run ``callback`` when Rack starts to shut down, while handles still work.

    Use it to stop and join threads you started: after the callbacks return,
    every handle to a Rack object raises :class:`~rackpy.errors.StaleHandleError`,
    and Rack goes on to delete its modules and engine. Callbacks run on the
    main thread, most recently registered first; one that raises is logged and
    the others still run. Returns ``callback``, so it works as a decorator.
    """
    _closing_callbacks.append(callback)
    return callback


def _run_closing_callbacks() -> None:  # pyright: ignore[reportUnusedFunction]
    while _closing_callbacks:
        callback = _closing_callbacks.pop()
        try:
            callback()
        except Exception as e:  # report and continue: Rack is shutting down regardless
            _rackpy_host.log_warning(f"on_context_closing callback {callback!r} raised {type(e).__name__}: {e}")
