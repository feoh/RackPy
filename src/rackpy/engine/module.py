# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned native modules, configured in a scoped construction callback (RP-14A).

Available inside Rack only. This is the construction/component-access slice:
model registration, parameter metadata and lifecycle adapters are still planned.
The callback passed to :meth:`rackpy.raw.engine.Module.create` is construction code, not DSP.
"""

from rackpy.raw import engine as _engine

Module = _engine.Module

__all__ = ["Module"]
