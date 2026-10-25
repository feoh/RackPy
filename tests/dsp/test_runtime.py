# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-17A: the native kernel runtime.

The native tests compare the built-in kernels with direct C++ sample for
sample and drive the engine/main-thread handover; the guard run proves the
kernel path makes no heap, lock, write or GIL calls; the in-Rack suite runs
kernels inside Rack's own engine.
"""

import json
import os
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack"


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
def test_the_guard_run_needs_the_preload(dev_build: Path) -> None:
    proc = subprocess.run([str(dev_build / "rackpy_kernel_guard")], capture_output=True, text=True, check=False)
    assert proc.returncode == 2 and "LD_PRELOAD" in proc.stderr


@pytest.mark.native
def test_kernel_objects_reference_no_python(dev_build: Path) -> None:
    objects = sorted(dev_build.glob("CMakeFiles/rackpy_host.dir/native/dsp/runtime/*.cpp.o"))
    objects += list(dev_build.glob("CMakeFiles/rackpy_host.dir/native/module/lifecycle.cpp.o"))
    assert len(objects) == 3, objects
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
