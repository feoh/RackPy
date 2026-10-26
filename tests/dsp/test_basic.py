# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-18: run the DSP suite (tests/dsp/in_rack_basic) inside Rack, on the production plugin.

The suite replays native/tests/dsp_reference.cpp, Rack's own DSP code compiled
with the SDK's flags, through rackpy.raw.dsp, and checks what the reference
cannot: refusals, buffers, templates and Python callbacks.
"""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack_basic"


def test_dsp_suite_passes_inside_rack(
    dev_build: Path, make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    reference = tmp_path / "dsp_reference.json"
    with reference.open("w") as f:
        subprocess.run([str(dev_build / "rackpy_dsp_reference")], stdout=f, check=True)
    result = run_pytest_in_rack(
        make_bundle(), rack_host, tmp_path / "user", SUITE, args=["--rackpy-reference", str(reference)]
    )
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert outcomes and set(outcomes.values()) == {"passed"}, result.summary()
    assert len(outcomes) >= 9
