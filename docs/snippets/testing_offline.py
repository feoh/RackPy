# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=python output=testing_offline.out
import itertools
import sys
from pathlib import Path

# The plugin's Python: in a plugin project of your own, MyPlugin/pythonpath.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "examples" / "oscillator" / "pythonpath"))
from rackpyoscillator_dsp import offline_reference


def rising_zero_crossings(samples: list[float]) -> int:
    return sum(1 for a, b in itertools.pairwise(samples) if a < 0 <= b)


def test_one_volt_is_one_octave() -> None:
    low = offline_reference(48000, sample_rate=48000, knob=0.0)
    high = offline_reference(48000, sample_rate=48000, knob=1.0)
    assert rising_zero_crossings([f[0] for f in low.outputs[0]]) == 261  # middle C: 261.63 Hz
    assert rising_zero_crossings([f[0] for f in high.outputs[0]]) == 523  # an octave up


def test_a_polyphonic_cable_gives_a_polyphonic_output() -> None:
    result = offline_reference(4, sample_rate=48000, voct=[0.0] * 4)
    assert all(len(frame) == 1 for frame in result.outputs[0])


test_one_volt_is_one_octave()
test_a_polyphonic_cable_gives_a_polyphonic_output()
print("both tests passed")
