# SPDX-License-Identifier: GPL-3.0-or-later
"""Session fixtures shared by the native and host tests."""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import BIN, MACOS, PLATFORM, RACK_HOST, REPO, copy_sharing_binaries, require_tools, tool

# Suites under */in_rack*/ run inside Rack (tests.support.run_pytest_in_rack); outside Rack they cannot import.
collect_ignore_glob = ["*/in_rack", "*/in_rack/*", "*/in_rack_*", "*/in_rack_*/*"]

CATEGORIES = ("unit", "native", "inventory", "host", "dist", "docs")


def pytest_collection_modifyitems(items: list[pytest.Item]) -> None:
    """Every test needs a category marker; CI jobs select by marker, so an unmarked test would never run."""
    unmarked = [item.nodeid for item in items if not any(item.get_closest_marker(c) for c in CATEGORIES)]
    if unmarked:
        raise pytest.UsageError(f"tests without one of the markers {CATEGORIES}: {unmarked[:10]}")


@pytest.fixture(scope="session")
def rack_sdk() -> Path:
    """The verified, extracted Rack SDK; downloads it into the cache on first use."""
    return Path(tool("rack-sdk", "path", PLATFORM).strip())


@pytest.fixture(scope="session")
def rack_host() -> Path:
    """The pinned Rack Free host directory."""
    return Path(tool("rack-sdk", "host", RACK_HOST).strip().splitlines()[-1])


@pytest.fixture(scope="session")
def native_tools() -> None:
    require_tools("cmake", "ninja", *(("c++", "otool", "nm") if MACOS else ("g++", "readelf", "nm", "objdump")))


@pytest.fixture(scope="session")
def dev_build(native_tools: None, tmp_path_factory: pytest.TempPathFactory) -> Path:
    """A host-compiler build of the skeleton, with its CTest suite run."""
    build = tmp_path_factory.mktemp("build")
    proc = subprocess.run(
        [str(BIN / "rackpy-dev"), "build", "--build-dir", str(build), "--werror"],
        capture_output=True,
        text=True,
        check=False,
        cwd=REPO,
    )
    assert proc.returncode == 0, proc.stdout[-4000:] + proc.stderr[-4000:]
    return build


@pytest.fixture(scope="session")
def make_bundle(dev_build: Path, tmp_path_factory: pytest.TempPathFactory) -> Callable[[], Path]:
    """Return a factory for fresh plugin directories (runtime symlinked) from the dev build."""

    pristine: list[Path] = []  # bundled once; every caller gets a copy that shares its binaries

    def make() -> Path:
        if not pristine:
            base = tmp_path_factory.mktemp("bundle-base")
            out = tool("rackpy-dev", "bundle", str(base), "--build-dir", str(dev_build))
            pristine.append(Path(out.strip().splitlines()[-1]))
        bundle = tmp_path_factory.mktemp("bundle") / pristine[0].name
        copy_sharing_binaries(pristine[0], bundle)
        return bundle

    return make


@pytest.fixture(scope="session")
def hooks_build(native_tools: None, tmp_path_factory: pytest.TempPathFactory) -> Path:
    build = tmp_path_factory.mktemp("hooks-build")
    cmd = [str(BIN / "rackpy-dev"), "build", "--build-dir", str(build), "--werror", "--test-hooks"]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False, cwd=REPO)
    assert proc.returncode == 0, proc.stdout[-4000:] + proc.stderr[-4000:]
    return build


@pytest.fixture(scope="session")
def make_hooks_bundle(hooks_build: Path, tmp_path_factory: pytest.TempPathFactory) -> Callable[[], Path]:
    pristine: list[Path] = []  # bundled once; every caller gets a copy that shares its binaries

    def make() -> Path:
        if not pristine:
            base = tmp_path_factory.mktemp("hooks-bundle-base")
            out = tool("rackpy-dev", "bundle", str(base), "--build-dir", str(hooks_build))
            pristine.append(Path(out.strip().splitlines()[-1]))
        bundle = tmp_path_factory.mktemp("hooks-bundle") / pristine[0].name
        copy_sharing_binaries(pristine[0], bundle)
        return bundle

    return make
