# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's triggers, pulse generator, timers and clock divider (RP-18B).

Rack's own objects (``rack::dsp``, also available as :mod:`rackpy.raw.dsp`),
each holding its C++ state, so they behave exactly as in a Rack module. For
vectors of four, subscript the templates: ``TSchmittTrigger[float_4]``,
``TTimer[float_4]``. For testing and offline processing; per-sample audio
belongs in a native kernel. Available inside Rack only.
"""

from rackpy.raw.dsp import (
    BooleanTrigger,
    ClockDivider,
    PulseGenerator,
    SchmittTrigger,
    Timer,
    TSchmittTrigger,
    TTimer,
)

__all__ = ["BooleanTrigger", "ClockDivider", "PulseGenerator", "SchmittTrigger", "TSchmittTrigger", "TTimer", "Timer"]
