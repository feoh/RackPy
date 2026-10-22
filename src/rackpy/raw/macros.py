# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's macros that have a meaning at run time (common.hpp, arch.hpp, math.hpp).

Constants come from the plugin as built (``ARCH_*`` exist only on their own
platform). Function-like macros map to functions or context managers with the
same effect. Macros that only exist for the C preprocessor or linker
(``CONCAT``, ``TOSTRING``, ``BINARY*``) have no Python counterpart; the
[values reference](https://github.com/feoh/RackPy/blob/main/docs/reference/values.md)
explains each one.
"""

import warnings
from collections.abc import Callable, Sized

from rackpy.raw import _arch
from rackpy.raw._common import DeferWrapper
from rackpy.raw.math import Rect, Vec

__all__ = ["DEFER", "DEPRECATED", "ENUMS", "LENGTHOF", "RECT_ARGS", "VEC_ARGS"]

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
