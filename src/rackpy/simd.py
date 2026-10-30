# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's 4-lane SIMD vectors and the math on them, as one tidy import.

``float_4`` and ``int32_4`` hold Rack's own C++ vectors (``rack::simd``, also
available as :mod:`rackpy.raw.simd`), and every operator and function runs
Rack's C++ code, so results match Rack's lane for lane. Comparisons return
masks, not bools. Each function also takes plain floats, as Rack's do, so
code written once works for a float and for a float_4. Available inside Rack
only.

Python calls cost far more than the four lanes they compute: use these to
configure, test and explore Rack's SIMD semantics, not for per-sample audio,
which belongs in a native kernel (see the DSP how-to).
"""

from rackpy.raw import simd as _raw
from rackpy.raw.simd import (
    Template,
    Vector,
    abs,
    andnot,
    arg,
    atan,
    atan2,
    ceil,
    clamp,
    cos,
    crossfade,
    exp,
    fabs,
    float_4,
    floor,
    fmax,
    fmin,
    fmod,
    hypot,
    ifelse,
    int32_4,
    log,
    log2,
    log10,
    movemask,
    movemask_inverse,
    pow,
    rcp,
    rescale,
    round,
    rsqrt,
    sgn,
    sin,
    sqrt,
    tan,
    trunc,
)

IMPLEMENTATION: str = _raw._IMPLEMENTATION  # pyright: ignore[reportPrivateUsage]
"""Rack's SIMD implementation this plugin was built with: "sse4.2" on x86-64, "simde" elsewhere."""

__all__ = [
    "IMPLEMENTATION",
    "Template",
    "Vector",
    "abs",
    "andnot",
    "arg",
    "atan",
    "atan2",
    "ceil",
    "clamp",
    "cos",
    "crossfade",
    "exp",
    "fabs",
    "float_4",
    "floor",
    "fmax",
    "fmin",
    "fmod",
    "hypot",
    "ifelse",
    "int32_4",
    "log",
    "log2",
    "log10",
    "movemask",
    "movemask_inverse",
    "pow",
    "rcp",
    "rescale",
    "round",
    "rsqrt",
    "sgn",
    "sin",
    "sqrt",
    "tan",
    "trunc",
]
