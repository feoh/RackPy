# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: RackPy under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer.

- Every native unit test (CTest) runs in an ASan+UBSan build and in a TSan build.
- The ASan+UBSan build of the test-hooks plugin runs RackPy's live-Rack suites inside
  Rack Free, with the sanitizer runtime preloaded (Rack itself is not instrumented, and
  Python's allocator is switched to malloc so ASan sees its memory). A control run
  proves the sanitizer is active: a deliberate heap overflow is reported and stops Rack.

TSan is not run inside Rack: neither Rack nor CPython is built with it, so it would
report the synchronization it cannot see. That gap is recorded in SECURITY.md and
planning/rp-38-validation.md; the native tests cover RackPy's own threaded code.
"""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import REPO, linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.sanitizer, linux_x64_only()]
ASAN = Path("/usr/lib/libasan.so.8")

# Live-Rack suites with no options of their own, run again on the sanitized plugin.
SUITES: dict[str, list[str]] = {
    "tests/app/in_rack": [],
    "tests/coverage/in_rack": [],
    "tests/dsp/in_rack": [],
    "tests/engine/ops/in_rack": [],
    "tests/engine/expanders/in_rack": [],
    "tests/history/in_rack": [],
    "tests/laboratory/in_rack": [],
    "tests/rendering/in_rack": [],
    "tests/ui/in_rack": [],
    "tests/widget/in_rack": ["-k", "not startup_only"],
}


def sanitizer_env(logs: Path) -> dict[str, str]:
    """ASan writes its reports under `logs`; UBSan, linked separately, writes them to stderr."""
    return {
        "LD_PRELOAD": str(ASAN),
        "ASAN_OPTIONS": f"detect_leaks=0:verify_asan_link_order=0:halt_on_error=1:log_path={logs / 'asan'}",
        "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
        "PYTHONMALLOC": "malloc",
    }


def reports(logs: Path, stderr: Path) -> str:
    """Every sanitizer report of the run: ASan's files, and UBSan's lines on stderr."""
    found = "".join(p.read_text(errors="replace") for p in logs.iterdir())
    text = stderr.read_text(errors="replace") if stderr.exists() else ""
    return found + "".join(line + "\n" for line in text.splitlines() if "runtime error" in line or "Sanitizer" in line)


def ctest(build: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["ctest", "--test-dir", str(build), "--output-on-failure"], capture_output=True, text=True, check=False
    )


@pytest.mark.native
def test_the_native_tests_under_asan_and_ubsan(asan_build: Path) -> None:
    proc = ctest(asan_build)
    assert proc.returncode == 0, proc.stdout[-6000:]
    assert "100% tests passed" in proc.stdout and "Sanitizer" not in proc.stdout + proc.stderr


@pytest.mark.native
def test_the_native_tests_under_tsan(tsan_build: Path) -> None:
    proc = ctest(tsan_build)
    assert proc.returncode == 0, proc.stdout[-6000:]
    assert "100% tests passed" in proc.stdout and "ThreadSanitizer" not in proc.stdout + proc.stderr


@pytest.mark.host
@pytest.mark.gui
def test_asan_is_active_in_rack(make_asan_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    """The control: a heap overflow in the sanitized plugin is reported, and halts Rack."""
    assert ASAN.is_file(), "AddressSanitizer's runtime (GCC's libasan.so.8) is needed"
    suite = tmp_path / "canary"
    suite.mkdir()
    (suite / "test_canary.py").write_text(
        "from _rackpy_host import _testing\n\ndef test_overflow() -> None:\n    _testing.sanitizer_canary()\n"
    )
    logs, stderr = tmp_path / "logs", tmp_path / "stderr.txt"
    logs.mkdir()
    with pytest.raises(AssertionError):  # Rack does not exit cleanly: ASan halts it
        run_pytest_in_rack(
            make_asan_bundle(),
            rack_host,
            tmp_path / "user",
            suite,
            during_ui=True,
            env=sanitizer_env(logs),
            stderr=stderr,
        )
    found = reports(logs, stderr)
    assert "heap-buffer-overflow" in found and "sanitizerCanary" in found, found[-3000:]


@pytest.mark.host
@pytest.mark.gui
@pytest.mark.parametrize("suite", sorted(SUITES))
def test_live_rack_suites_under_asan_and_ubsan(
    make_asan_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, suite: str
) -> None:
    logs, stderr = tmp_path / "logs", tmp_path / "stderr.txt"
    logs.mkdir()
    result = run_pytest_in_rack(
        make_asan_bundle(),
        rack_host,
        tmp_path / "user",
        REPO / suite,
        args=SUITES[suite],
        during_ui=True,
        timeout=900,
        env=sanitizer_env(logs),
        stderr=stderr,
    )
    found = reports(logs, stderr)
    assert found == "", found[-6000:]
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert outcomes and set(outcomes.values()) <= {"passed", "skipped"}, result.summary()
