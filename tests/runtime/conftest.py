# SPDX-License-Identifier: GPL-3.0-or-later
"""A test-hooks build of the plugin: the RuntimeProbe module and _rackpy_host._testing."""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import BIN, REPO, tool


@pytest.fixture(scope="session")
def hooks_build(native_tools: None, tmp_path_factory: pytest.TempPathFactory) -> Path:
    build = tmp_path_factory.mktemp("hooks-build")
    cmd = [str(BIN / "rackpy-dev"), "build", "--build-dir", str(build), "--werror", "--test-hooks"]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False, cwd=REPO)
    assert proc.returncode == 0, proc.stdout[-4000:] + proc.stderr[-4000:]
    return build


@pytest.fixture(scope="session")
def make_hooks_bundle(hooks_build: Path, tmp_path_factory: pytest.TempPathFactory) -> Callable[[], Path]:
    def make() -> Path:
        dest = tmp_path_factory.mktemp("hooks-bundle")
        return Path(tool("rackpy-dev", "bundle", str(dest), "--build-dir", str(hooks_build)).strip().splitlines()[-1])

    return make
