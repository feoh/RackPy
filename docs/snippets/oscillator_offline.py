# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=python output=oscillator_offline.out
import itertools
import sys
from pathlib import Path

# The tutorial's plugin keeps its Python in examples/oscillator/pythonpath.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "examples" / "oscillator" / "pythonpath"))
from rackpyoscillator_dsp import offline_reference

# One second at 48 kHz, with the knob at 0 V and nothing patched into the input.
result = offline_reference(48000, sample_rate=48000)
sine = [frame[0] for frame in result.outputs[0]]
print("first samples:", [round(v, 3) for v in sine[:4]])
print("peak:", round(max(sine), 3), "trough:", round(min(sine), 3))
rises = sum(1 for a, b in itertools.pairwise(sine) if a < 0 <= b)
print("cycles in one second:", rises)
lit = sum(1 for v in result.lights[0] if v > 0.5)
print("light on for", lit, "of", len(result.lights[0]), "frames")

# The knob one volt higher: one octave up.
up = offline_reference(48000, sample_rate=48000, knob=1.0)
up_rises = sum(1 for a, b in itertools.pairwise(up.outputs[0]) if a[0] < 0 <= b[0])
print("one volt up:", up_rises, "cycles")
