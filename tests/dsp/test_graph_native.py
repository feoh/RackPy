# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-21B: the native plan interpreter.

The native tests compare graph plans with direct C++ twins sample for
sample; the guard run proves a 16-channel plan with every node type makes no
heap, lock, write or GIL calls while its controls change and it is replaced;
the plan's object file references no CPython symbols. The in-Rack half is
tests/dsp/in_rack/test_graph.py, run by test_runtime.py's in-Rack suite.
"""

import json
import os
import subprocess
from pathlib import Path

import pytest

from tests.support import linux_x64_only

pytestmark = [linux_x64_only(), pytest.mark.native]


def test_plans_match_direct_cpp(dev_build: Path) -> None:
    proc = subprocess.run([str(dev_build / "rackpy_graph_tests")], capture_output=True, text=True, check=False)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "graph plans: all checks passed" in proc.stdout


def test_a_sixteen_channel_plan_makes_no_forbidden_calls(dev_build: Path) -> None:
    env = {**os.environ, "LD_PRELOAD": str(dev_build / "audio_guard.so")}
    out = subprocess.run([str(dev_build / "rackpy_kernel_guard")], capture_output=True, text=True, check=True, env=env)
    report = json.loads(out.stdout)
    assert not any(report["graph"].values()), report["graph"]
    assert not any(report["compiled"].values()), report["compiled"]  # RP-21C: a compiled kernel, both paths
    assert report["inject"]["malloc"] >= report["frames"]  # the guard is live in this run


def test_the_plan_cost_benchmark_runs(dev_build: Path) -> None:
    """Timings are recorded in benchmarks/rp21-graph-cost.json, not asserted: CI is not controlled hardware."""
    out = subprocess.run([str(dev_build / "rackpy_graph_bench")], capture_output=True, text=True, check=True)
    results = {r["workload"]: r for r in json.loads(out.stdout)["results"]}
    assert set(results) == {
        f"{tier}_{w}" for tier in ("plan", "compiled") for w in ("sine", "sine_gain", "vca", "poly16")
    }
    assert all(r["plan_p50_ns"] > 0 and r["native_p50_ns"] > 0 for r in results.values())


def test_plan_objects_reference_no_python(dev_build: Path) -> None:
    objects = sorted(dev_build.glob("CMakeFiles/rackpy_host.dir/native/dsp/graph/*.cpp.o"))
    assert [o.name for o in objects] == ["plan.cpp.o"]
    bin_dir = Path(__file__).resolve().parents[2] / "bin"
    proc = subprocess.run(
        [str(bin_dir / "rackpy-dev"), "audio-symbols", *map(str, objects)], capture_output=True, text=True, check=False
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
