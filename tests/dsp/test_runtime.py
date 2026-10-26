# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-17A: the native kernel runtime.

The native tests compare the built-in kernels with direct C++ sample for
sample and drive the engine/main-thread handover; the guard run proves the
kernel path makes no heap, lock, write or GIL calls; the in-Rack suite runs
kernels inside Rack's own engine.
"""

import json
import os
import shutil
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack"
LIBRARY_SUITE = Path(__file__).resolve().parent / "in_rack_libraries"
LIBRARIES = [
    "example",
    "wrong_abi",
    "throws",
    "duplicate",
    "reserved",
    "bad_name",
    "graph_wrong_abi",
    "graph_bad_node",
    "graphs",
]


@pytest.mark.native
def test_kernels_match_direct_cpp_and_hand_over_safely(dev_build: Path) -> None:
    proc = subprocess.run([str(dev_build / "rackpy_kernel_tests")], capture_output=True, text=True, check=False)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "all checks passed" in proc.stdout


@pytest.mark.native
def test_the_kernel_path_makes_no_forbidden_calls(dev_build: Path) -> None:
    env = {**os.environ, "LD_PRELOAD": str(dev_build / "audio_guard.so")}
    out = subprocess.run([str(dev_build / "rackpy_kernel_guard")], capture_output=True, text=True, check=True, env=env)
    report = json.loads(out.stdout)
    assert not any(report["production"].values()), report  # pickup, controls, retirement, two kernel types
    assert not any(report["failure"].values()), report  # silence and the diagnostic
    assert report["inject"]["malloc"] >= report["frames"] and report["inject"]["free"] >= report["frames"]


@pytest.mark.native
def test_the_overhead_benchmark_runs(dev_build: Path) -> None:
    """Timings are recorded in benchmarks/rp17-kernel-overhead.json, not asserted: CI is not controlled hardware."""
    out = subprocess.run([str(dev_build / "rackpy_kernel_bench")], capture_output=True, text=True, check=True)
    results = {r["workload"]: r for r in json.loads(out.stdout)["results"]}
    assert set(results) == {"same_sine", "same_gain", "builtin_sine", "builtin_gain"}
    assert all(r["rackpy_p50_ns"] > 0 and r["native_p50_ns"] > 0 for r in results.values())


@pytest.mark.native
def test_the_guard_run_needs_the_preload(dev_build: Path) -> None:
    proc = subprocess.run([str(dev_build / "rackpy_kernel_guard")], capture_output=True, text=True, check=False)
    assert proc.returncode == 2 and "LD_PRELOAD" in proc.stderr


@pytest.mark.native
def test_kernel_objects_reference_no_python(dev_build: Path) -> None:
    objects = sorted(dev_build.glob("CMakeFiles/rackpy_host.dir/native/dsp/runtime/*.cpp.o"))
    objects += list(dev_build.glob("CMakeFiles/rackpy_host.dir/native/module/lifecycle.cpp.o"))
    assert len(objects) == 4, objects  # runtime, builtin, libraries, lifecycle
    bin_dir = Path(__file__).resolve().parents[2] / "bin"
    proc = subprocess.run(
        [str(bin_dir / "rackpy-dev"), "audio-symbols", *map(str, objects)], capture_output=True, text=True, check=False
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr


@pytest.mark.host
@pytest.mark.gui
def test_kernels_in_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
    ran = {name.split("::", 1)[0].rsplit("/", 1)[-1] for name in result.outcomes()}
    assert ran == {p.name for p in SUITE.glob("test_*.py")}, f"every in-Rack test file reports: {sorted(ran)}"


@pytest.mark.host
@pytest.mark.gui
def test_kernel_libraries_load_and_bad_ones_are_skipped(
    make_hooks_bundle: Callable[[], Path], hooks_build: Path, rack_host: Path, tmp_path: Path
) -> None:
    bundle = make_hooks_bundle()
    (bundle / "kernels").mkdir()
    for name in LIBRARIES:
        shutil.copy(hooks_build / f"kernels_{name}.so", bundle / "kernels")
    (tmp_path / "outside.so").write_bytes((hooks_build / "kernels_example.so").read_bytes())
    (bundle / "kernels" / "escape.so").symlink_to(tmp_path / "outside.so")
    config = json.loads((bundle / "rackpy-loader.json").read_text())
    config["kernels"] = [
        *(f"kernels/kernels_{name}.so" for name in LIBRARIES),
        "kernels/missing.so",
        "../RackPy/kernels/kernels_example.so",
        "kernels/escape.so",
        3,
    ]
    (bundle / "rackpy-loader.json").write_text(json.dumps(config))
    user = tmp_path / "user"
    result = run_pytest_in_rack(bundle, rack_host, user, LIBRARY_SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
    log = result.run
    assert log.lines("kernel type example.Fold from kernels/kernels_example.so")
    assert log.lines("graph node type example.Hold from kernels/kernels_example.so")
    assert log.lines("kernel type graphs.Tutorial from kernels/kernels_graphs.so")
    expected = {
        "kernels_wrong_abi.so": "built for kernel ABI 999",
        "kernels_throws.so": "registration raised: kernel-library-intentional-error",
        "kernels_duplicate.so": "kernel type example.Fold is already registered",
        "kernels_reserved.so": "namespace rackpy is reserved",
        "kernels_bad_name.so": 'kernel type name "not.good" is not an identifier',
        "kernels_graph_wrong_abi.so": "built for graph ABI 999",
        "kernels_graph_bad_node.so": "graph node type graphbad.Wide reads 9 signals; at most 8",
        "missing.so": "no such file",
        "../RackPy/kernels/kernels_example.so": 'without ".."',
        "escape.so": "resolves outside the plugin folder",
    }
    for library, reason in expected.items():
        assert any(library in line and reason in line for line in log.lines("RackPy skipped kernel library")), library
    assert log.lines(f'"kernels" entry #{len(config["kernels"]) - 1} is not a path')
