# SPDX-License-Identifier: GPL-3.0-or-later
"""Ports, parameters and lights (RP-13). Available inside Rack only.

These are Rack's ``rack::engine`` component types (also :mod:`rackpy.raw.engine`):
each call runs Rack's code. Beyond Rack's methods they offer properties for
everyday use (``port.voltage``, ``port.channel_count``, ``port.connected``,
``light.brightness``). Components a Rack module owns are valid while the module
exists and are used on Rack's main thread; components you create are yours.
"""

from rackpy.raw.engine import PORT_MAX_CHANNELS, Input, Light, LightInfo, Output, Param, Port, PortInfo

__all__ = ["PORT_MAX_CHANNELS", "Input", "Light", "LightInfo", "Output", "Param", "Port", "PortInfo"]
