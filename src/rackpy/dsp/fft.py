# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's FFTs and FIR convolution (RP-19A).

Rack's own PFFFT wrappers (``rack::dsp``, also available as
:mod:`rackpy.raw.dsp`): ``RealFFT`` and ``ComplexFFT`` transform float32
buffers, which PFFFT needs 16-byte aligned (an ``array.array`` or numpy array
is); ``RealTimeConvolver`` convolves a block at a time with a long impulse
response. Lengths and buffers are checked before Rack sees them. For analysis,
tables and offline processing; per-sample audio belongs in a native kernel.
Available inside Rack only.
"""

from rackpy.raw.dsp import ComplexFFT, RealFFT, RealTimeConvolver, boxcar_lowpass_ir, convolve_naive

__all__ = ["ComplexFFT", "RealFFT", "RealTimeConvolver", "boxcar_lowpass_ir", "convolve_naive"]
