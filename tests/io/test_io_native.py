# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-31J: native audio-port and MIDI-input I/O for kernels and graphs, without Rack.

The native tests compare a module's audio port with Core Audio's own code
block for block (native/tests/test_io.cpp); the guard checks the exchange
code for heap, lock, write and GIL calls, but does not cover live Rack device
getters or Rack's own master-clock methods; the stress runs cross the driver, engine and
main threads under ASan/UBSan and TSan; the I/O
objects reference no CPython symbols. tests/io/test_io_paths.py runs them in
Rack.
"""

import json
import os
import subprocess
from pathlib import Path

import pytest

from tests.support import ASAN_OPTIONS, linux_x64_only

pytestmark = [linux_x64_only(), pytest.mark.native]


def test_native_audio_and_midi_match_rack(dev_build: Path) -> None:
    proc = subprocess.run([str(dev_build / "rackpy_io_tests")], capture_output=True, text=True, check=False)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert "io paths: all checks passed" in proc.stdout


def test_the_audio_paths_make_no_forbidden_calls(dev_build: Path) -> None:
    env = {**os.environ, "LD_PRELOAD": str(dev_build / "audio_guard.so")}
    out = subprocess.run([str(dev_build / "rackpy_kernel_guard")], capture_output=True, text=True, check=True, env=env)
    report = json.loads(out.stdout)
    # As master and secondary, resampled and copied, a missed block, stream starts and stops, rackpy.AudioInterface.
    assert not any(report["audio"].values()) and report["audio_flowed"], report
    assert not any(report["midi_burst"].values()) and report["midi_burst_worked"], report
    assert report["inject"]["malloc"] >= report["frames"]  # the guard is live in this run


@pytest.mark.parametrize(("variant", "blocks"), [("asan", 30000), ("tsan", 30000)])
def test_the_paths_under_sanitizers(dev_build: Path, variant: str, blocks: int) -> None:
    env = {
        **os.environ,
        "ASAN_OPTIONS": ASAN_OPTIONS,
        "UBSAN_OPTIONS": "print_stacktrace=1",
        "TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1",
    }
    proc = subprocess.run(
        [str(dev_build / f"rackpy_io_stress_{variant}"), str(blocks)],
        capture_output=True,
        text=True,
        env=env,
        check=False,
        timeout=600,
    )
    report = proc.stdout + proc.stderr[-4000:]
    assert proc.returncode == 0, report
    assert "io stress: all checks passed" in proc.stdout, report
    for sanitizer in ("runtime error", "AddressSanitizer", "ThreadSanitizer", "LeakSanitizer"):
        assert sanitizer not in proc.stderr, report


def test_bridge_calls_racks_public_master_clock(dev_build: Path) -> None:
    obj = dev_build / "CMakeFiles/rackpy_host.dir/native/io/audio_bridge.cpp.o"
    proc = subprocess.run(["nm", "-uC", str(obj)], capture_output=True, text=True, check=True)
    for method in ("stepBlock", "setMasterModule", "setMasterModule_NoLock", "setSuggestedSampleRate"):
        assert f"rack::engine::Engine::{method}(" in proc.stdout


def test_io_objects_reference_no_python(dev_build: Path) -> None:
    objects = sorted(dev_build.glob("CMakeFiles/rackpy_host.dir/native/io/*.cpp.o"))
    assert [o.name for o in objects] == ["audio_bridge.cpp.o", "endpoints.cpp.o", "midi_queue.cpp.o"]
    bin_dir = Path(__file__).resolve().parents[2] / "bin"
    proc = subprocess.run(
        [str(bin_dir / "rackpy-dev"), "audio-symbols", *map(str, objects)], capture_output=True, text=True, check=False
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
