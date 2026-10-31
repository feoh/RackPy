# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-19: Rack's inline DSP code under AddressSanitizer and UndefinedBehaviorSanitizer.

native/tests/dsp_reference.cpp, built with -fsanitize=address,undefined,
runs every case of the DSP reference (including the FFT, convolver, ring
buffer and resampler boundaries) with Rack's inline code instrumented; any
out-of-bounds access, leak or undefined behaviour fails the run. libRack's
own compiled code (PFFFT, minBlepImpulse) is not instrumented. The in-Rack
replay of the same cases is tests/dsp/test_basic.py.
"""

import json
import os
import subprocess
from pathlib import Path

import pytest

from tests.support import ASAN_OPTIONS, rack_platforms

pytestmark = [pytest.mark.native, rack_platforms()]


def test_dsp_reference_runs_clean_under_sanitizers(dev_build: Path) -> None:
    env = {**os.environ, "ASAN_OPTIONS": ASAN_OPTIONS, "UBSAN_OPTIONS": "print_stacktrace=1"}
    sanitized = subprocess.run(
        [str(dev_build / "rackpy_dsp_reference_sanitized")], capture_output=True, text=True, env=env, check=False
    )
    assert sanitized.returncode == 0, sanitized.stderr[-4000:]
    assert "runtime error" not in sanitized.stderr and "AddressSanitizer" not in sanitized.stderr, sanitized.stderr[
        -4000:
    ]
    plain = subprocess.run([str(dev_build / "rackpy_dsp_reference")], capture_output=True, text=True, check=True)
    names = [case["fn"] for case in json.loads(sanitized.stdout)["cases"]]
    assert names == [case["fn"] for case in json.loads(plain.stdout)["cases"]], "both builds run the same cases"
