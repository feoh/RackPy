# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's filters: RC, exponential, peak, slew, IIR and biquad (RP-18C).

Rack's own objects (``rack::dsp``, also available as :mod:`rackpy.raw.dsp`),
each holding its C++ state. For vectors of four, subscript the templates:
``TBiquadFilter[float_4]``; IIR filters of any order from 2 to 8 are
``IIRFilter[b_order, a_order]``. :func:`process_block` runs a float filter
over a whole buffer natively. For design, analysis and offline processing;
per-sample audio belongs in a native kernel. Available inside Rack only.
"""

from collections.abc import Buffer
from typing import Any

from rackpy.raw import dsp as _raw
from rackpy.raw.dsp import (
    BiquadFilter,
    ExponentialFilter,
    ExponentialSlewLimiter,
    IIRFilter,
    PeakFilter,
    RCFilter,
    SlewLimiter,
    TBiquadFilter,
    TExponentialFilter,
    TExponentialSlewLimiter,
    TPeakFilter,
    TRCFilter,
    TSlewLimiter,
)

__all__ = [
    "BiquadFilter",
    "ExponentialFilter",
    "ExponentialSlewLimiter",
    "IIRFilter",
    "PeakFilter",
    "RCFilter",
    "SlewLimiter",
    "TBiquadFilter",
    "TExponentialFilter",
    "TExponentialSlewLimiter",
    "TPeakFilter",
    "TRCFilter",
    "TSlewLimiter",
    "process_block",
]

_TIMED = (ExponentialFilter, PeakFilter, SlewLimiter, ExponentialSlewLimiter)


def process_block(filter: Any, in_: Buffer, out: Buffer, delta_time: float | None = None) -> None:
    """Run a float filter over every sample of ``in_``, writing ``out``, natively.

    Each sample goes through the same Rack call as ``filter.process``, in
    order, so the results and the filter's final state are exactly those of a
    Python loop of ``process`` calls, only much faster. ``in_`` and ``out`` are
    float32 buffers; ``out`` may be ``in_``. The exponential, peak and slew
    filters need ``delta_time``; the IIR and biquad filters take none. RC
    filters have no single output, so they are not supported.
    """
    if isinstance(filter, _TIMED):
        if delta_time is None:
            raise TypeError(f"process_block: a {type(filter).__name__} needs delta_time")
        _raw._process_block(filter, in_, out, delta_time)
    else:
        if delta_time is not None:
            raise TypeError(f"process_block: a {type(filter).__name__} takes no delta_time")
        _raw._process_block(filter, in_, out)
