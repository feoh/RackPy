# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38 fixtures: sanitizer builds of RackPy, made once per session."""

import json
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import BIN, REPO, copy_sharing_binaries, tool


def _run(cmd: list[str], timeout: float = 3600) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False, cwd=REPO, timeout=timeout)
    assert proc.returncode == 0, " ".join(cmd) + "\n" + proc.stdout[-4000:] + proc.stderr[-4000:]
    return proc


@pytest.fixture(scope="session")
def asan_build(native_tools: None, tmp_path_factory: pytest.TempPathFactory) -> Path:
    """Every native target, the test-hooks plugin included, under AddressSanitizer and UBSan (not tested here)."""
    build = tmp_path_factory.mktemp("asan-build")
    _run(
        [
            str(BIN / "rackpy-dev"),
            "build",
            "--build-dir",
            str(build),
            "--test-hooks",
            "--sanitize",
            "address,undefined",
            "--no-test",
        ]
    )
    return build


@pytest.fixture(scope="session")
def tsan_build(native_tools: None, tmp_path_factory: pytest.TempPathFactory) -> Path:
    """The native unit tests (CTest) under ThreadSanitizer: only their executables are built."""
    build = tmp_path_factory.mktemp("tsan-build")
    _run([str(BIN / "rackpy-dev"), "configure", "--build-dir", str(build), "--sanitize", "thread"])
    tests = json.loads(_run(["ctest", "--test-dir", str(build), "--show-only=json-v1"]).stdout)["tests"]
    targets = sorted({Path(t["command"][0]).name for t in tests})
    _run(["cmake", "--build", str(build), "--target", *targets])
    return build


@pytest.fixture(scope="session")
def make_asan_bundle(asan_build: Path, tmp_path_factory: pytest.TempPathFactory) -> Callable[[], Path]:
    pristine: list[Path] = []

    def make() -> Path:
        if not pristine:
            base = tmp_path_factory.mktemp("asan-bundle-base")
            out = tool("rackpy-dev", "bundle", str(base), "--build-dir", str(asan_build))
            pristine.append(Path(out.strip().splitlines()[-1]))
        bundle = tmp_path_factory.mktemp("asan-bundle") / pristine[0].name
        copy_sharing_binaries(pristine[0], bundle)
        return bundle

    return make
