# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's macros that have a meaning at run time (common.hpp, arch.hpp, math.hpp, logger.hpp).

Constants come from the plugin as built (``ARCH_*`` exist only on their own
platform). Function-like macros map to functions or context managers with the
same effect. Macros that only exist for the C preprocessor or linker
(``CONCAT``, ``TOSTRING``, ``BINARY*``) have no Python counterpart; the
[values reference](https://github.com/feoh/RackPy/blob/main/docs/reference/values.md)
explains each one.
"""

import sys
import warnings
from collections.abc import Callable, Sized

from rackpy.raw import _arch
from rackpy.raw._common import DeferWrapper
from rackpy.raw.math import Rect, Vec

__all__ = [
    "DEBUG",
    "DEFER",
    "DEPRECATED",
    "ENUMS",
    "FATAL",
    "INFO",
    "LENGTHOF",
    "RACK_HELD",
    "RACK_MOD_ALT",
    "RACK_MOD_ALT_NAME",
    "RACK_MOD_CTRL",
    "RACK_MOD_CTRL_NAME",
    "RACK_MOD_MASK",
    "RACK_MOD_SHIFT",
    "RACK_MOD_SHIFT_NAME",
    "RECT_ARGS",
    "VEC_ARGS",
    "WARN",
]

# widget/event.hpp's key modifiers, with GLFW's values (GLFW_MOD_SHIFT 1, CONTROL 2, ALT 4, SUPER 8).
# Rack's "Ctrl" is the Command key on macOS.
RACK_MOD_SHIFT = 0x0001
RACK_MOD_SHIFT_NAME = "Shift"
RACK_MOD_ALT = 0x0004
RACK_MOD_ALT_NAME = "Alt"
RACK_MOD_CTRL = 0x0008 if hasattr(_arch, "ARCH_MAC") else 0x0002
RACK_MOD_CTRL_NAME = "⌘" if hasattr(_arch, "ARCH_MAC") else "Ctrl"
RACK_MOD_MASK = 0x0001 | 0x0002 | 0x0004 | 0x0008
"""Filters the real modifier keys out of a mods value (no Caps Lock or Num Lock)."""
RACK_HELD = 3
"""A key action meaning the key is (still) held, next to GLFW's PRESS 1, RELEASE 0 and REPEAT 2."""

for _name in ("ARCH_X64", "ARCH_ARM64", "ARCH_WIN", "ARCH_MAC", "ARCH_LIN"):
    if hasattr(_arch, _name):
        globals()[_name] = getattr(_arch, _name)
        __all__.append(_name)


def DEFER(f: Callable[[], object]) -> DeferWrapper:
    """``DEFER({ ... })``: run ``f`` when the ``with`` block exits."""
    return DeferWrapper(f)


def DEPRECATED[F: Callable[..., object]](obj: F) -> F:
    """``DEPRECATED``: mark a function or class deprecated; using it emits DeprecationWarning."""
    name = getattr(obj, "__qualname__", repr(obj))
    return warnings.deprecated(f"{name} is deprecated")(obj)


def ENUMS(name: str, count: int, start: int = 0) -> dict[str, int]:
    """``ENUMS(name, count)``: reserve ``count`` consecutive IDs from ``start``.

    In C++, ``enum { A, ENUMS(B, 3), C }`` gives A = 0, B = 1, B_LAST = 3 and
    C = 4. Here ``ENUMS("B", 3, start=1)`` returns ``{"B": 1, "B_LAST": 3}``;
    the next free ID is ``start + count``.
    """
    return {name: start, f"{name}_LAST": start + count - 1}


def LENGTHOF(arr: Sized) -> int:
    """``LENGTHOF(arr)``: the number of elements, ``len(arr)``."""
    return len(arr)


def VEC_ARGS(v: Vec) -> tuple[float, float]:
    """``VEC_ARGS(v)``: ``(v.x, v.y)``."""
    return (v.x, v.y)


def RECT_ARGS(r: Rect) -> tuple[float, float, float, float]:
    """``RECT_ARGS(r)``: ``(r.pos.x, r.pos.y, r.size.x, r.size.y)``."""
    return (r.pos.x, r.pos.y, r.size.x, r.size.y)


def _log(level_name: str, format: str, args: tuple[int | float | str | bytes, ...]) -> None:
    from rackpy.raw import logger  # the native module, bound with rackpy.raw

    caller = sys._getframe(2)  # who called DEBUG/INFO/WARN/FATAL, as __FILE__, __LINE__ and __FUNCTION__ name it
    logger.log(
        getattr(logger.Level, level_name),
        caller.f_code.co_filename,
        caller.f_lineno,
        caller.f_code.co_name,
        format,
        *args,
    )


def DEBUG(format: str, *args: int | float | str | bytes) -> None:
    """Rack's DEBUG: logs at debug level with the caller's file, line and function; ``format`` is printf's."""
    _log("DEBUG_LEVEL", format, args)


def INFO(format: str, *args: int | float | str | bytes) -> None:
    """Rack's INFO."""
    _log("INFO_LEVEL", format, args)


def WARN(format: str, *args: int | float | str | bytes) -> None:
    """Rack's WARN."""
    _log("WARN_LEVEL", format, args)


def FATAL(format: str, *args: int | float | str | bytes) -> None:
    """Rack's FATAL: logs at fatal level (Rack's macro does not stop Rack either)."""
    _log("FATAL_LEVEL", format, args)
