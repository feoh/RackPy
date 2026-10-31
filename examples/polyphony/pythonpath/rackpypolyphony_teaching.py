"""The polyphonic sine oscillator's two loops in plain Python, for reading and offline tests.

**Teaching only: not real-time, and Rack never runs this.** The plugin's modules run
kernels/polyphony.cpp natively. These functions show the same algorithm: how many
voices a cable gives, one phase per voice, and how SIMD code handles voices four at a
time, including a last group that is only partly used. They use Python's floats
(doubles), so they agree with the C++ to about 1e-4 V, not bit for bit. This module
does not import Rack.
"""

import math
from collections.abc import Sequence

FREQ_C4 = 261.6256  # rack::dsp::FREQ_C4: 0 V is middle C


def voices(cable_channels: int) -> int:
    """As many voices as the V/OCT cable has channels, and one when it is unpatched (0)."""
    return max(1, cable_channels)


def per_channel(
    knob: float, inputs: Sequence[Sequence[float]], sample_rate: float, amplitude: float = 5.0
) -> list[list[float]]:
    """Each frame's output, one voice at a time. `inputs` is each frame's V/OCT channels."""
    phases = [0.0] * 16
    frames: list[list[float]] = []
    for volts in inputs:
        out: list[float] = []
        for c in range(voices(len(volts))):
            # A mono cable feeds every voice (Rack's getPolyVoltage); unpatched reads 0 V.
            v = volts[c] if c < len(volts) else (volts[0] if len(volts) == 1 else 0.0)
            phases[c] += FREQ_C4 * 2.0 ** (knob + v) / sample_rate
            phases[c] -= math.floor(phases[c])
            out.append(amplitude * math.sin(2 * math.pi * phases[c]))
        frames.append(out)
    return frames


def four_at_a_time(
    knob: float, inputs: Sequence[Sequence[float]], sample_rate: float, amplitude: float = 5.0
) -> list[list[float]]:
    """The same, in groups of four voices, as the float_4 code computes them.

    Each group is one SIMD register: its four phases advance together. When the voice
    count is not a multiple of four, the last group computes voices that do not exist
    and the output simply has fewer channels.
    """
    groups = [[0.0] * 4 for _ in range(4)]
    frames: list[list[float]] = []
    for volts in inputs:
        n = voices(len(volts))
        out: list[float] = []
        for first in range(0, n, 4):
            group = groups[first // 4]
            lanes = [first + lane for lane in range(4)]
            pitch = [knob + (volts[c] if c < len(volts) else (volts[0] if len(volts) == 1 else 0.0)) for c in lanes]
            for lane in range(4):
                group[lane] += FREQ_C4 * 2.0 ** pitch[lane] / sample_rate
                group[lane] -= math.floor(group[lane])
            out += [amplitude * math.sin(2 * math.pi * p) for p in group]
        frames.append(out[:n])
    return frames
