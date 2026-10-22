# SPDX-License-Identifier: GPL-3.0-or-later
"""The low-level mirror of the Rack API, named after Rack's C++ namespaces.

``rack::math`` is ``rackpy.raw.math``, ``rack::color`` is ``rackpy.raw.color``
and the root ``rack`` namespace is ``rackpy.raw`` itself. Functions and methods
use snake_case; classes and constants keep their C++ names. Available only
inside Rack. The [binding coverage](https://github.com/feoh/RackPy/blob/main/docs/reference/coverage.md)
lists what is bound.
"""

import sys as _sys
from types import ModuleType as _ModuleType
from typing import Any as _Any

try:
    import _rackpy_host
except ImportError as e:
    raise ImportError("rackpy.raw is available only inside Rack, in a RackPy plugin") from e

__all__: list[str] = []

for _name, _value in sorted(vars(_rackpy_host.raw).items()):
    if _name.startswith("__"):
        continue
    if isinstance(_value, _ModuleType):
        _sys.modules[f"{__name__}.{_name}"] = _value
    globals()[_name] = _value
    if not _name.startswith("_"):
        __all__.append(_name)

from rackpy.raw._common import DeferWrapper, Exception, construct, defer_wrapper, get  # noqa: E402

__all__ += ["DeferWrapper", "Exception", "construct", "defer_wrapper", "get"]

from rackpy.raw import macros as macros  # noqa: E402

__all__.append("macros")

# Rack also calls these Quantity methods on its engine thread (Core MIDI-Map, ADR 003), where Python
# never runs, so they have no Python path. A subclass that defines one would silently never be called.
_ENGINE_THREAD_METHODS = ("get_value", "set_value", "get_min_value", "get_max_value", "get_default_value")


def _refuse_engine_thread_overrides(cls: type, **kwargs: _Any) -> None:
    defined = [name for name in _ENGINE_THREAD_METHODS if name in vars(cls)]
    if defined:
        raise TypeError(
            f"{cls.__qualname__} defines {', '.join(defined)}: Rack can call these on its engine thread, where "
            "Python never runs, so a Python Quantity cannot override them. Subclass rackpy.types.ValueQuantity, "
            "which stores its value natively, and override the display methods instead."
        )


Quantity.__init_subclass__ = classmethod(_refuse_engine_thread_overrides)  # type: ignore[assignment]  # noqa: F821
