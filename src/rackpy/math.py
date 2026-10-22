# SPDX-License-Identifier: GPL-3.0-or-later
"""2D vectors, rectangles and the scalar helpers Rack uses, as one tidy import.

These are Rack's own implementations (``rack::math``, also available as
:mod:`rackpy.raw.math`): every call runs Rack's C++ code, so results match
Rack's exactly, including its float32 rounding. Available inside Rack only.

``Vec`` is a mutable value; ``Rect.pos`` and ``Rect.size`` are live views of
the rectangle's fields. See the values reference for the copy-versus-view rules.
"""

from rackpy.raw.math import (
    Rect,
    Vec,
    chop,
    clamp,
    clamp_safe,
    complex_mult,
    crossfade,
    euc_div,
    euc_div_mod,
    euc_mod,
    interpolate_linear,
    is_even,
    is_near,
    is_odd,
    is_pow2,
    log2,
    normalize_zero,
    rescale,
    sgn,
)

__all__ = [
    "Rect",
    "Vec",
    "chop",
    "clamp",
    "clamp_safe",
    "complex_mult",
    "crossfade",
    "euc_div",
    "euc_div_mod",
    "euc_mod",
    "interpolate_linear",
    "is_even",
    "is_near",
    "is_odd",
    "is_pow2",
    "log2",
    "normalize_zero",
    "rescale",
    "sgn",
]
