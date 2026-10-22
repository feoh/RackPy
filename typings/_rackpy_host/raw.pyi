# SPDX-License-Identifier: GPL-3.0-or-later
# The native module behind rackpy.raw. Its typed surface is src/rackpy/raw/*.pyi;
# rackpy.raw copies these attributes, so code should import rackpy.raw instead.
from typing import Any

def __getattr__(name: str) -> Any: ...
