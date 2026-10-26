# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's DSP helpers: conversions, scalings, polynomials, windows and ODE steps (RP-18A).

These are Rack's own implementations (``rack::dsp``, also available as
:mod:`rackpy.raw.dsp`); every call runs Rack's C++ code. Each function takes a
float or a :class:`rackpy.simd.float_4`, for Rack's two instantiations. Use
them to design and test, to fill tables and to analyse; per-sample audio
belongs in a native kernel. Available inside Rack only.
"""

from rackpy.raw.dsp import (
    FREQ_A4,
    FREQ_C4,
    FREQ_SEMITONE,
    Frame,
    Int24,
    amplitude_to_db,
    blackman,
    blackman_harris,
    blackman_harris_window,
    blackman_nuttall,
    blackman_nuttall_window,
    blackman_window,
    convert,
    cubic,
    db_to_amplitude,
    exp2_floor,
    exp2_taylor5,
    exponential_bipolar,
    hann,
    hann_window,
    poly_direct,
    poly_estrin,
    poly_horner,
    quadratic_bipolar,
    quartic_bipolar,
    quintic,
    sinc,
    sqrt_bipolar,
    step_euler,
    step_rk2,
    step_rk4,
)

__all__ = [
    "FREQ_A4",
    "FREQ_C4",
    "FREQ_SEMITONE",
    "Frame",
    "Int24",
    "amplitude_to_db",
    "blackman",
    "blackman_harris",
    "blackman_harris_window",
    "blackman_nuttall",
    "blackman_nuttall_window",
    "blackman_window",
    "convert",
    "cubic",
    "db_to_amplitude",
    "exp2_floor",
    "exp2_taylor5",
    "exponential_bipolar",
    "hann",
    "hann_window",
    "poly_direct",
    "poly_estrin",
    "poly_horner",
    "quadratic_bipolar",
    "quartic_bipolar",
    "quintic",
    "sinc",
    "sqrt_bipolar",
    "step_euler",
    "step_rk2",
    "step_rk4",
]
