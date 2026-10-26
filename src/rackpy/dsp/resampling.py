# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's MinBLEP and resamplers (RP-19B).

Rack's own objects (``rack::dsp``, also available as :mod:`rackpy.raw.dsp`):
``MinBlepGenerator[z, o]`` for band-limited steps, ``SampleRateConverter[channels]``
(speexdsp), and ``Decimator[oversample, quality]`` and ``Upsampler[...]`` for
integer factors. The templates take a bounded set of sizes; see the DSP
reference. For tests and offline processing; per-sample audio belongs in a
native kernel. Available inside Rack only.
"""

from rackpy.raw.dsp import Decimator, MinBlepGenerator, SampleRateConverter, Upsampler, min_blep_impulse

__all__ = ["Decimator", "MinBlepGenerator", "SampleRateConverter", "Upsampler", "min_blep_impulse"]
