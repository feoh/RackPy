# SPDX-License-Identifier: GPL-3.0-or-later
"""The low-level mirror of the Rack API: one submodule per family, named as in C++.

Available only inside Rack. **Nothing is bound yet**; see the
[binding coverage](https://github.com/feoh/RackPy/blob/main/docs/reference/coverage.md)
for what exists. Each family's submodule appears here as its bindings land.
"""

import sys as _sys
from types import ModuleType as _ModuleType

try:
    import _rackpy_host
except ImportError as e:
    raise ImportError("rackpy.raw is available only inside Rack, in a RackPy plugin") from e

__all__: list[str] = []

for _name, _value in sorted(vars(_rackpy_host.raw).items()):
    if isinstance(_value, _ModuleType):
        _sys.modules[f"{__name__}.{_name}"] = _value
        globals()[_name] = _value
        __all__.append(_name)
