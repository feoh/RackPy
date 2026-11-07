# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-52: independent envelope reference behavior and Rack host integration."""

from itertools import pairwise

import pytest
import rackpyenvelope_reference as reference

pytestmark = pytest.mark.unit


def run(
    trigger: list[float], *, attack: float, decay: float, loop: bool = False, rate: int = 48_000
) -> list[tuple[float, float, float]]:
    model = reference.ADReference()
    return [model.process(v, attack, decay, loop, rate) for v in trigger]


def test_trigger_produces_attack_then_decay_and_inverted_output() -> None:
    samples = run([0.0, 10.0, *([0.0] * 12_000)], attack=0.2, decay=0.2)
    envelope = [row[0] for row in samples]
    inverted = [row[1] for row in samples]
    assert envelope[1] > 0.0
    assert max(envelope) == 10.0
    assert envelope[-1] == 0.0
    assert all(a + b == pytest.approx(10.0) for a, b in zip(envelope, inverted, strict=True))


def test_eoc_is_a_short_pulse_after_the_decay() -> None:
    samples = run([0.0, 10.0, *([0.0] * 30_000)], attack=0.0, decay=0.0)
    eoc = [index for index, row in enumerate(samples) if row[2] > 0]
    assert eoc
    assert len(eoc) == 48
    assert eoc == list(range(eoc[0], eoc[0] + len(eoc)))
    assert samples[eoc[0]][0] == 0.0


def test_loop_restarts_without_a_second_trigger() -> None:
    samples = run([0.0, 10.0, *([0.0] * 30_000)], attack=0.0, decay=0.0, loop=True)
    peaks = [i for i, row in enumerate(samples) if row[0] >= 9.99]
    assert len(peaks) >= 3
    assert all(b - a > 1 for a, b in pairwise(peaks))


def test_a_held_trigger_is_not_repeated_as_multiple_edges() -> None:
    samples = run([0.0, *([10.0] * 10_000)], attack=0.0, decay=0.0)
    eoc = [row for row in samples if row[2] > 0]
    assert len(eoc) == 48
