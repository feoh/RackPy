# SPDX-License-Identifier: GPL-3.0-or-later
"""Signal statistics for the oscillator example: frequency, amplitude, continuity and the blink.

Pure Python, used outside Rack and copied into the in-Rack suite. Each check raises
AssertionError naming what is out of tolerance.
"""

import itertools
import math
from collections.abc import Sequence

FREQ_C4 = 261.6256
AMPLITUDE = 5.0
DRIFT_VOLTS = 0.05  # C++ reference against the graph over 1 s: measured at most 0.014 V (planning/rp-33-validation.md)


def stream(rate: int, kind: str) -> tuple[list[float], list[float] | None]:
    """The fixed input streams: one second each of knob volts per frame and the V/oct input (None: unpatched).

    ``unpatched``: the knob at 0, 1 and -1 V for a third each (C4, C5, C3).
    ``patched``: the knob at 0 V and the input at 0.5 V, then -0.25 V, for half each.
    """
    if kind == "unpatched":
        third = rate // 3
        return [0.0] * third + [1.0] * third + [-1.0] * (rate - 2 * third), None
    half = rate // 2
    return [0.0] * rate, [0.5] * half + [-0.25] * (rate - half)


def segments(knob: Sequence[float], voct: Sequence[float] | None) -> list[tuple[int, int, float]]:
    """(start, end, pitch volts) of each stretch where the pitch is constant."""
    pitch = [k + (voct[i] if voct is not None else 0.0) for i, k in enumerate(knob)]
    found: list[tuple[int, int, float]] = []
    start = 0
    for i in range(1, len(pitch) + 1):
        if i == len(pitch) or pitch[i] != pitch[start]:
            found.append((start, i, pitch[start]))
            start = i
    return found


def frequency(samples: Sequence[float], rate: float) -> float:
    """Frequency from the first to the last rising zero crossing, interpolated between samples."""
    crossings = [
        i - 1 + samples[i - 1] / (samples[i - 1] - samples[i])
        for i in range(1, len(samples))
        if samples[i - 1] < 0.0 <= samples[i]
    ]
    assert len(crossings) >= 3, f"only {len(crossings)} rising zero crossings"
    return (len(crossings) - 1) * rate / (crossings[-1] - crossings[0])


def check_sine(sine: Sequence[float], rate: float, knob: Sequence[float], voct: Sequence[float] | None) -> None:
    """1 V/octave from middle C, +-5 V, and no step larger than the steepest slope allows."""
    for start, end, volts in segments(knob, voct):
        expected = FREQ_C4 * 2.0**volts
        part = sine[start + 1 : end]
        measured = frequency(part, rate)
        assert abs(measured - expected) / expected < 2e-4, f"{volts} V: {measured} Hz, expected {expected} Hz"
        assert max(part) <= AMPLITUDE + 1e-5 and min(part) >= -AMPLITUDE - 1e-5, f"{volts} V: beyond +-5 V"
        nearest = AMPLITUDE * math.cos(math.pi * expected / rate)  # the peak sample is at most half a step away
        assert max(part) >= nearest - 1e-4 and min(part) <= -nearest + 1e-4, f"{volts} V: peaks below {nearest} V"
        slope = 2 * math.pi * AMPLITUDE * expected / rate
        worst = max(abs(b - a) for a, b in itertools.pairwise(part))
        assert worst <= slope * (1 + 1e-3), f"{volts} V: a step of {worst} V, more than {slope} V"


def check_blink(lights: Sequence[float], rate: float) -> None:
    """On for the first half of each second, off for the second: 1 Hz, 50% duty."""
    assert set(lights) <= {0.0, 1.0}, "the light is not just on and off"
    changes = [i for i in range(1, len(lights)) if lights[i] != lights[i - 1]]
    assert lights[0] == 1.0 and changes, "the light does not start lit and change"
    # Summing a float32 sample time drifts by up to 0.1% a second (31 frames at 48 kHz); a C++ module's does too.
    slack = 2e-3 * rate
    for k, i in enumerate(changes):
        assert abs(i - (k + 1) * rate / 2) <= slack, f"change {k} at frame {i}, expected near {(k + 1) * rate / 2}"
    on = sum(lights[: int(rate)])
    assert abs(on - rate / 2) <= slack, f"lit {on} of {rate} frames"


def check_parity(sine: Sequence[float], lights: Sequence[float], reference: Sequence[tuple[float, float]]) -> float:
    """Within DRIFT_VOLTS of the C++ reference at every frame, and the same light; returns the largest difference."""
    assert len(sine) == len(reference)
    worst = max(abs(s - r[0]) for s, r in zip(sine, reference, strict=True))
    assert worst <= DRIFT_VOLTS, f"differs from the C++ reference by {worst} V"
    assert list(lights) == [r[1] for r in reference], "the light differs from the C++ reference"
    return worst
