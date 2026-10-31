"""The Filter's processing in Python, offline, with Rack's own filter classes.

**Teaching only: not real-time.** Rack's engine never runs this; the plugin's modules run
kernels/dsp.cpp natively. This loop calls the same Rack code the kernel does,
``TBiquadFilter[float_4]`` and ``ClockDivider`` through rackpy.dsp, one frame at a
time from Python, and shows how the kernel works: four channels per filter,
coefficients every 32 frames. It computes the same samples to within about 1e-3 V: the
filter code is Rack's in both, but compiled separately (in RackPy and in the kernel
library) with the SDK's -funsafe-math-optimizations, which lets the compiler round
differently, and a resonant filter amplifies that. It needs Rack (rackpy.dsp's classes
are Rack's), so run it inside Rack, on the main thread.
"""

import math
import struct
from collections.abc import Sequence

from rackpy.dsp.filters import BiquadFilter, TBiquadFilter
from rackpy.raw.dsp import ClockDivider
from rackpy.simd import float_4

TYPES = (BiquadFilter.Type.LOWPASS, BiquadFilter.Type.HIGHPASS, BiquadFilter.Type.BANDPASS, BiquadFilter.Type.NOTCH)
UPDATE_FRAMES = 32


def f32(x: float) -> float:
    """x rounded to a C++ float, as the kernel's arithmetic rounds each step."""
    rounded: float = struct.unpack("f", struct.pack("f", x))[0]
    return rounded


def normalized_cutoff(octaves: float, sample_rate: float) -> float:
    """The kernel's `min(20.f * std::pow(2.f, octaves) / sampleRate, 0.45f)`, in float32 as it computes it."""
    hertz = f32(20.0 * f32(math.pow(2.0, f32(octaves))))
    return min(f32(hertz / f32(sample_rate)), f32(0.45))


def run(
    cutoff: float | Sequence[float],
    resonance: float,
    mode: int,
    inputs: Sequence[Sequence[float]],
    sample_rate: float,
) -> list[list[float]]:
    """Each frame's output for each frame's input channels (0 to 16), as the kernel computes it.

    ``cutoff`` is the CUTOFF knob, or its value at each frame when it moves.
    """
    knob = [float(cutoff)] * len(inputs) if isinstance(cutoff, int | float) else list(cutoff)
    filters = [TBiquadFilter[float_4]() for _ in range(4)]
    divider = ClockDivider()
    divider.set_division(UPDATE_FRAMES)
    fresh = True
    frames: list[list[float]] = []
    for k, volts in enumerate(inputs):
        if divider.process() or fresh:
            f = normalized_cutoff(knob[k], sample_rate)
            for flt in filters:
                flt.set_parameters(TYPES[mode], f, resonance, 1.0)
            fresh = False
        n = max(1, len(volts))
        out: list[float] = []
        for first in range(0, n, 4):
            # A mono cable feeds every channel (getPolyVoltageSimd); unpatched reads 0 V.
            lanes = [
                volts[0] if len(volts) == 1 else (volts[c] if c < len(volts) else 0.0) for c in range(first, first + 4)
            ]
            result = filters[first // 4].process(float_4(*lanes))
            out += list(result.s)
        frames.append(out[:n])
    return frames


def tone(frames: int, sample_rate: float, hz: float, channels: int) -> list[list[float]]:
    """A test signal: a sine at `hz` on each channel, a quarter-cycle apart per channel."""
    return [
        [5.0 * math.sin(2 * math.pi * (hz * k / sample_rate + c / 4)) for c in range(channels)] for k in range(frames)
    ]
