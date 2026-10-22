# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-07: the audio-path guard catches forbidden calls, and RackPy-shaped kernels match native output.

    uv run pytest tests/integration/test_perf_guard.py -v

Builds benchmarks/spike against the pinned SDK. Timing numbers are not asserted
here; bin/perf-baseline records them.
"""

import json
import os
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest

REPO = Path(__file__).resolve().parents[2]
SPIKE = REPO / "benchmarks" / "spike"
pytestmark = [
    pytest.mark.native,
    pytest.mark.skipif(sys.platform != "linux" or platform.machine() != "x86_64", reason="Linux x86_64 only"),
]


def tool(*args: str) -> str:
    return subprocess.run([str(REPO / "bin" / args[0]), *args[1:]], capture_output=True, text=True, check=True).stdout


@pytest.fixture(scope="module")
def build(tmp_path_factory: pytest.TempPathFactory) -> Path:
    out = tmp_path_factory.mktemp("perf-build")
    sdk = tool("rack-sdk", "path").strip()
    subprocess.run(
        ["make", "-C", str(SPIKE), f"RACK_DIR={sdk}", f"BUILD={out}", f"{out}/bench", f"{out}/audio_guard.so"],
        check=True,
        capture_output=True,
    )
    return out


def run(build: Path, mode: str, guard: bool = False) -> dict[str, Any]:
    env = {**os.environ, "LD_PRELOAD": str(build / "audio_guard.so")} if guard else None
    out = subprocess.run([str(build / "bench"), mode], capture_output=True, text=True, check=True, env=env).stdout
    data: dict[str, Any] = json.loads(out)
    return data


def test_production_shapes_make_no_forbidden_calls(build: Path) -> None:
    runs = run(build, "guard", guard=True)["runs"]
    production = [r for r in runs if r["workload"] != "inject"]
    assert production
    for r in production:
        assert not any(r["events"].values()), r


@pytest.mark.parametrize(
    ("variant", "event"), [("malloc", "malloc"), ("new", "malloc"), ("mutex", "mutex_lock"), ("write", "write")]
)
def test_guard_detects_injected_violations(build: Path, variant: str, event: str) -> None:
    runs = run(build, "guard", guard=True)
    (r,) = [r for r in runs["runs"] if r["workload"] == "inject" and r["variant"] == variant]
    assert r["events"][event] >= runs["frames"], r


def test_guard_requires_preload(build: Path) -> None:
    proc = subprocess.run([str(build / "bench"), "guard"], capture_output=True, text=True, check=False)
    assert proc.returncode == 2 and "LD_PRELOAD" in proc.stderr


def test_kernels_match_native_output(build: Path) -> None:
    pairs = run(build, "equivalence")["pairs"]
    for p in pairs:
        if p["workload"] == "chain":
            # Node-separated plans round differently from the fused native chain under
            # the SDK's -funsafe-math-optimizations; they must stay within 1 ULP.
            assert p["max_ulp_difference"] <= 1, p
        else:
            assert p["frames_with_any_bit_difference"] == 0, p
