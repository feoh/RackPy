# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-18: run the DSP suite (tests/dsp/in_rack_basic) inside Rack, on the production plugin.

The suite replays native/tests/dsp_reference.cpp, Rack's own DSP code compiled
with the SDK's flags, through rackpy.raw.dsp, and checks what the reference
cannot: refusals, buffers, templates and Python callbacks.
"""

import json
import os
import platform
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, rack_platforms()]
SUITE = Path(__file__).resolve().parent / "in_rack_basic"


def test_dsp_suite_passes_inside_rack(
    dev_build: Path, make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    reference = tmp_path / "dsp_reference.json"
    with reference.open("w") as f:
        subprocess.run([str(dev_build / "rackpy_dsp_reference")], stdout=f, check=True)
    cost = tmp_path / "call_cost.json"
    args = ["--rackpy-reference", str(reference), "--rackpy-call-cost", str(cost)]
    result = run_pytest_in_rack(make_bundle(), rack_host, tmp_path / "user", SUITE, args=args)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert outcomes and set(outcomes.values()) == {"passed"}, result.summary()
    assert len(outcomes) >= 17
    ran = {name.split("::", 1)[0].rsplit("/", 1)[-1] for name in outcomes}
    assert ran == {p.name for p in SUITE.glob("test_*.py")}, f"every in-Rack test file reports: {sorted(ran)}"
    # RACKPY_CALL_COST_OUT keeps the timings, with this machine's details (planning/rp-18c-validation.md).
    target = os.environ.get("RACKPY_CALL_COST_OUT")
    if target:
        data = json.loads(cost.read_text())
        data["environment"] = environment()
        Path(target).write_text(json.dumps(data, indent=1) + "\n")


def environment() -> dict[str, str]:
    def read(path: str) -> str:
        try:
            return Path(path).read_text().strip()
        except OSError:
            return "unknown"

    cpu = next(
        (line.split(":", 1)[1].strip() for line in read("/proc/cpuinfo").splitlines() if line.startswith("model name")),
        "unknown",
    )
    return {
        "cpu": cpu,
        "kernel": platform.release(),
        "governor": read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"),
        "load_average_1min": read("/proc/loadavg").split(" ")[0],
        "where": "inside Rack Free 2.6.6 (headless), Rack's main thread at plugin start-up, production build",
    }
