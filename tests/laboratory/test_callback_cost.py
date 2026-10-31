# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the real laboratory benchmark in Rack; optionally retain raw data, like RP-18C."""

import hashlib
import json
import os
import shlex
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.dsp.test_basic import environment
from tests.support import REPO, rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, rack_platforms()]


def test_laboratory_callback_cost(
    dev_build: Path, make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    output = tmp_path / "callback_cost.json"
    result = run_pytest_in_rack(
        make_bundle(),
        rack_host,
        tmp_path / "user",
        Path(__file__).parent / "in_rack_cost",
        args=["--laboratory-cost-out", str(output)],
    )
    assert result.exit == 0, result.summary()
    assert list(result.outcomes().values()) == ["passed"], result.summary()
    data = json.loads(output.read_text())
    assert len(data["raw_runs_in_execution_order"]) == 84
    target = os.environ.get("RACKPY_LABORATORY_COST_OUT")
    if target:
        data["environment"] = environment()
        for name, command in {
            "revision": ["git", "rev-parse", "HEAD"],
            "compiler": ["g++", "--version"],
            "machine": ["uname", "-a"],
            "cpu_affinity": ["taskset", "-pc", str(os.getpid())],
        }.items():
            data["environment"][name] = subprocess.check_output(command, text=True, cwd=REPO).strip()
        commands = subprocess.check_output(["ninja", "-C", str(dev_build), "-t", "commands"], text=True)
        data["environment"]["laboratory_compile_command"] = next(
            line for line in commands.splitlines() if " -c " in line and "/native/laboratory/module.cpp" in line
        )
        data["source_sha256"] = {
            name: hashlib.sha256((REPO / name).read_bytes()).hexdigest()
            for name in (
                "tests/laboratory/in_rack_cost/test_callback_cost.py",
                "native/laboratory/module.cpp",
                "native/laboratory/bench.cpp",
            )
        }
        for name, path in {
            "energy_preference": "/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference",
            "turbo_disabled": "/sys/devices/system/cpu/intel_pstate/no_turbo",
        }.items():
            data["environment"][name] = Path(path).read_text().strip() if Path(path).exists() else "unknown"
        # The invocation that actually ran (benchmarks/rp22-laboratory-cost.json came from the full host suite).
        data["command"] = f"RACKPY_LABORATORY_COST_OUT={shlex.quote(target)} " + shlex.join(sys.argv)
        Path(target).write_text(json.dumps(data, indent=2) + "\n")
