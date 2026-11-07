# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline behavioral checks for the independent Feedback Governor example."""

from __future__ import annotations

import importlib.util
import math
import sys
from pathlib import Path
from typing import Any

import pytest

EXAMPLE = Path(__file__).resolve().parents[2] / "examples" / "feedback-governor"
REFERENCE_PATH = EXAMPLE / "pythonpath" / "rackpyfeedbackgovernor" / "reference.py"
_spec = importlib.util.spec_from_file_location("feedback_governor_reference", REFERENCE_PATH)
assert _spec is not None and _spec.loader is not None
_reference = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _reference
_spec.loader.exec_module(_reference)
FeedbackGovernorReference = _reference.FeedbackGovernorReference


def process(ref: Any, **overrides: object) -> float:
    settings: dict[str, object] = {
        "send": 0.0,
        "returned": 0.0,
        "feedback": 1.0,
        "cap": 10.0,
        "tone": 20000.0,
        "decay": 1.0,
        "sample_rate": 48000.0,
    }
    settings.update(overrides)
    return float(ref.process(**settings))


@pytest.mark.unit
def test_governor_applies_return_gain_and_per_pass_decay() -> None:
    full = FeedbackGovernorReference()
    decayed = FeedbackGovernorReference()
    first_sample = 1.0 - math.exp(-2.0 * math.pi * 20000.0 / 48000.0)
    assert process(full, returned=4.0) == pytest.approx(4.0 * first_sample, abs=1e-12)
    assert process(decayed, returned=4.0, decay=0.25) == pytest.approx(1.0 * first_sample, abs=1e-12)
    assert process(FeedbackGovernorReference(), returned=4.0, feedback=0.5) == pytest.approx(
        2.0 * first_sample, abs=1e-12
    )


@pytest.mark.unit
def test_cv_adds_control_and_output_cap_bounds_both_polarities() -> None:
    ref = FeedbackGovernorReference()
    assert abs(process(ref, send=8.0, returned=8.0, feedback=1.0, cap=3.0)) <= 3.0
    assert abs(process(ref, send=-8.0, returned=-8.0, feedback=1.0, cap=3.0)) <= 3.0
    cv_only = FeedbackGovernorReference()
    first_sample = 1.0 - math.exp(-2.0 * math.pi * 20000.0 / 48000.0)
    assert process(cv_only, returned=5.0, feedback=0.0, cv=5.0) == pytest.approx(2.5 * first_sample, abs=1e-12)


@pytest.mark.unit
def test_tone_filter_attenuates_high_frequency_return() -> None:
    sample_rate = 48000.0
    hot = FeedbackGovernorReference()
    mellow = FeedbackGovernorReference()
    hot_energy = []
    mellow_energy = []
    for frame in range(12000):
        wave = math.sin(2.0 * math.pi * 8000.0 * frame / sample_rate)
        hot_energy.append(process(hot, returned=wave, tone=12000.0))
        mellow_energy.append(process(mellow, returned=wave, tone=500.0))
    assert max(map(abs, mellow_energy[-1000:])) < 0.2 * max(map(abs, hot_energy[-1000:]))


@pytest.mark.unit
def test_dc_blocker_rejects_steady_return_and_reset_clears_history() -> None:
    ref = FeedbackGovernorReference()
    values = [process(ref, returned=5.0) for _ in range(24000)]
    assert abs(values[-1]) < 0.02
    assert process(ref, returned=5.0, kill=True) == 0.0
    assert process(ref, returned=0.0) == 0.0


@pytest.mark.unit
def test_sample_rate_change_requires_a_fresh_reference_state() -> None:
    ref = FeedbackGovernorReference()
    process(ref, returned=5.0, sample_rate=44100.0)
    ref.reset()
    assert process(ref, returned=0.0, sample_rate=96000.0) == 0.0


@pytest.mark.unit
def test_example_has_independent_provenance_and_native_kernel() -> None:
    provenance = (EXAMPLE / "PROVENANCE.md").read_text()
    source = (EXAMPLE / "kernels" / "governor.cpp").read_text()
    assert "06794bca8ccbc3b413b929713017a252783dce51" in provenance
    assert "independent" in provenance and "do not endorse" in provenance
    assert "#include <rack.hpp>" in source
    assert "rackpy::Kernel" in source and "process(rack::engine::Module" in source
    assert "python.h" not in source.lower() and "insect" in provenance.lower()
