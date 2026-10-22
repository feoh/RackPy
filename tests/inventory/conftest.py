# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the RP-05 inventory tests.

uv run --with pytest --with clang==21.1.7 --with cyclopts pytest tests/inventory -v
"""

import importlib.util
import shutil
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest

REPO = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "minisdk"


@pytest.fixture(scope="session")
def inventory() -> ModuleType:
    if shutil.which("clang++") is None:
        pytest.skip("clang++ is required")
    spec = importlib.util.spec_from_file_location("inventory_sdk", REPO / "bin" / "inventory_sdk.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # dataclasses resolve their module through sys.modules
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def minisdk(tmp_path: Path) -> Path:
    dest = tmp_path / "minisdk"
    shutil.copytree(FIXTURE, dest)
    return dest


@pytest.fixture
def build(inventory: ModuleType) -> Callable[[Path], dict[str, Any]]:
    def run(sdk: Path) -> dict[str, Any]:
        doc: dict[str, Any] = inventory.build(sdk, None, "clang++", ("lin-x64",))
        return doc

    return run


@pytest.fixture(scope="session")
def real_sdk() -> Path:
    try:
        out = subprocess.run([str(REPO / "bin" / "rack-sdk"), "path"], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as e:
        pytest.skip(f"pinned SDK unavailable: {e}")
    return Path(out.stdout.strip())
