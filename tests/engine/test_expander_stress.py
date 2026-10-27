# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-23C: expander exchange under ASan/UBSan and TSan (native/tests/expander_stress.cpp).

Worker threads step a row of RackPy modules as Rack's engine does, while the
main thread removes, inserts and reorders modules, mixes in foreign and
mismatched ones, toggles snapshots, and a reader thread takes snapshots as
Python does. No Rack host: Rack's engine is reproduced, not run.
"""

import os
import subprocess
from pathlib import Path

import pytest

from tests.support import linux_x64_only

pytestmark = [linux_x64_only(), pytest.mark.native]


@pytest.mark.parametrize(("variant", "workers", "blocks"), [("asan", 1, 1500), ("asan", 4, 3000), ("tsan", 4, 1500)])
def test_hot_changes_and_snapshots_under_sanitizers(dev_build: Path, variant: str, workers: int, blocks: int) -> None:
    env = {
        **os.environ,
        "ASAN_OPTIONS": "detect_leaks=1:abort_on_error=0",
        "UBSAN_OPTIONS": "print_stacktrace=1",
        "TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1",
    }
    proc = subprocess.run(
        [str(dev_build / f"rackpy_expander_stress_{variant}"), str(workers), str(blocks)],
        capture_output=True,
        text=True,
        env=env,
        check=False,
        timeout=600,
    )
    report = proc.stdout + proc.stderr[-4000:]
    assert proc.returncode == 0, report
    assert "expander stress: all checks passed" in proc.stdout, report
    for sanitizer in ("runtime error", "AddressSanitizer", "ThreadSanitizer", "LeakSanitizer"):
        assert sanitizer not in proc.stderr, report
