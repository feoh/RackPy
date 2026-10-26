# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's VU meter and its MIDI generator and parser (RP-18D).

Rack's own objects (``rack::dsp``, also available as :mod:`rackpy.raw.dsp`),
each holding its C++ state. ``MidiGenerator[channels]`` turns gates and CV
into MIDI messages, which ``take_messages()`` returns; ``MidiParser[channels]``
turns messages into gates, CV and pulses, as Rack's MIDI to CV does. For 1 to
16 channels. For tests and offline processing; per-sample work belongs in a
native kernel. Available inside Rack only.
"""

from rackpy.raw.dsp import MidiGenerator, MidiParser, VuMeter2

__all__ = ["MidiGenerator", "MidiParser", "VuMeter2"]
