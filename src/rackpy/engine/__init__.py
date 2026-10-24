# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's engine objects, as one tidy import. Available inside Rack only.

Ports, parameters and lights (:mod:`rackpy.engine.components`), plus scoped
module construction (:mod:`rackpy.engine.module`). Models are registered with
:mod:`rackpy.plugin`; cables and engine operations remain planned.
"""

from rackpy.engine.components import PORT_MAX_CHANNELS, Input, Light, LightInfo, Output, Param, Port, PortInfo
from rackpy.engine.module import Module
from rackpy.raw.engine import ParamQuantity, SwitchQuantity

__all__ = [
    "PORT_MAX_CHANNELS",
    "Input",
    "Light",
    "LightInfo",
    "Module",
    "Output",
    "Param",
    "ParamQuantity",
    "Port",
    "PortInfo",
    "SwitchQuantity",
]
