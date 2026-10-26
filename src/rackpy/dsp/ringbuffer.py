# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's ring buffers (RP-19C).

Rack's own objects (``rack::dsp``, also available as :mod:`rackpy.raw.dsp`):
``RingBuffer[T, S]``, a single-producer single-consumer queue,
``DoubleRingBuffer[T, S]``, which keeps a linear copy, and
``AppleRingBuffer[T, S, N]``, a window sliding along a block. ``T`` is float,
``float_4`` or ``Frame[2]``, ``Frame[8]`` or ``Frame[16]``; see the DSP
reference for the sizes. Available inside Rack only.
"""

from rackpy.raw.dsp import AppleRingBuffer, DoubleRingBuffer, RingBuffer

__all__ = ["AppleRingBuffer", "DoubleRingBuffer", "RingBuffer"]
