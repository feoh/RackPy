# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
import array

from rackpy.simd import float_4, ifelse, movemask, sin

# Four lanes at once; a scalar broadcasts, as in Rack's C++.
phase = float_4(0.0, 0.25, 0.6, 0.75)
wave = 5.0 * sin(2 * 3.14159265 * phase)

# A comparison gives a mask, not a bool: select lanes with it.
mask = wave > 0.0
clipped = ifelse(mask, wave, 0.0)
assert movemask(mask) == 0b0010  # only lane 1 is above zero

# Load from and store to any float32 buffer, x[0] in lane 0.
out = array.array("f", [0.0] * 4)
clipped.store(out)
assert out[1] == clipped[1] and out[0] == 0.0
