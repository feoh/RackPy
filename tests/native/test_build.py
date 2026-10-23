# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-08 native checks: the skeleton builds with the SDK's flags, its binaries have the
expected dynamic shape, and missing or wrong inputs are setup errors.

These run outside Rack; tests/integration/test_host_smoke.py loads the result into Rack.
"""

import json
import shutil
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import BIN, REPO, linux_x64_only, require_tools

pytestmark = [pytest.mark.native, linux_x64_only()]


def configure(build: Path, *args: str) -> subprocess.CompletedProcess[str]:
    require_tools("cmake", "ninja")
    return subprocess.run(
        ["cmake", "-S", str(REPO), "-B", str(build), "-G", "Ninja", *args], capture_output=True, text=True, check=False
    )


def errors(proc: subprocess.CompletedProcess[str]) -> str:
    """CMake's error text with its line wrapping undone."""
    return " ".join(proc.stderr.split())


def test_build_has_no_warnings_and_passes_ctest(dev_build: Path) -> None:
    # dev_build configures with RACKPY_WERROR=ON and runs CTest; reaching here means both passed.
    assert (dev_build / "plugin.so").is_file()
    assert (dev_build / "rackpy_host.so").is_file()


def test_module_lifecycle_adapters_reference_no_python(dev_build: Path) -> None:
    # RP-14C3: Rack may dispatch sample-rate and master events on an audio
    # driver thread, so the native adapters' object must not touch CPython.
    objects = list(dev_build.glob("CMakeFiles/rackpy_host.dir/native/module/lifecycle.cpp.o"))
    assert len(objects) == 1, objects
    proc = subprocess.run(
        [str(BIN / "rackpy-dev"), "audio-symbols", str(objects[0])], capture_output=True, text=True, check=False
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr


def test_bundle_has_the_expected_dynamic_shape(make_bundle: Callable[[], Path]) -> None:
    plugin = make_bundle()
    proc = subprocess.run(
        [str(BIN / "rackpy-dev"), "inspect", str(plugin)], capture_output=True, text=True, check=False
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
    report = json.loads(proc.stdout[proc.stdout.index("{") :])
    assert report["plugin.so"]["python_references"] == []
    assert sorted(report["rackpy_host.so"]["exports"]) == ["rackpy_host_destroy", "rackpy_host_init"]
    assert "libpython3.14.so.1.0" in report["rackpy_host.so"]["needed"]
    layout = {p.relative_to(plugin).as_posix() for p in plugin.rglob("*") if "python/" not in p.as_posix()}
    expected = {"plugin.so", "rackpy_host.so", "plugin.json", "rackpy-loader.json", "LICENSE", "NOTICE"}
    assert expected <= layout
    assert "pythonpath/rackpy/__init__.py" in layout
    assert (plugin / "python").is_symlink()
    manifest = json.loads((plugin / "plugin.json").read_text())
    assert manifest["modules"] == [] and manifest["name"] == "RackPy"
    assert b"RuntimeProbe" not in (plugin / "rackpy_host.so").read_bytes(), "test hooks leaked into a production build"


def test_inspect_rejects_a_newer_glibc_than_allowed(make_bundle: Callable[[], Path]) -> None:
    plugin = make_bundle()
    cmd = [str(BIN / "rackpy-dev"), "inspect", str(plugin), "--max-glibc", "2.0"]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    assert proc.returncode != 0
    assert "newer than 2.0" in errors(proc)


def test_missing_sdk_is_a_setup_error(tmp_path: Path) -> None:
    proc = configure(tmp_path / "build", "-DRACK_SDK_DIR=")
    assert proc.returncode != 0
    assert "RACK_SDK_DIR is not set" in errors(proc)
    assert "bin/rack-sdk path" in errors(proc)


def test_unpinned_sdk_is_rejected(tmp_path: Path) -> None:
    fake = tmp_path / "sdk"
    (fake / "include").mkdir(parents=True)
    (fake / "include" / "rack.hpp").write_text("// not the pinned SDK\n")
    (fake / "libRack.so").write_bytes(b"not a library")
    proc = configure(tmp_path / "build", f"-DRACK_SDK_DIR={fake}")
    assert proc.returncode != 0
    assert "not the pinned" in errors(proc)


def test_modified_sdk_header_is_rejected(rack_sdk: Path, tmp_path: Path) -> None:
    copy = tmp_path / "sdk"
    shutil.copytree(rack_sdk, copy, symlinks=True, ignore=shutil.ignore_patterns("*.a"))
    header = copy / "include" / "engine" / "Module.hpp"
    header.write_text(header.read_text() + "\n// one changed byte is enough\n")
    proc = configure(tmp_path / "build", f"-DRACK_SDK_DIR={copy}")
    assert proc.returncode != 0
    assert "include/engine/Module.hpp does not match its pinned SHA-256" in errors(proc)


def test_missing_runtime_is_a_setup_error(rack_sdk: Path, tmp_path: Path) -> None:
    proc = configure(tmp_path / "build", f"-DRACK_SDK_DIR={rack_sdk}", "-DRACKPY_PYTHON_HOME=")
    assert proc.returncode != 0
    assert "RACKPY_PYTHON_HOME is not set" in errors(proc)


def test_build_type_is_refused(rack_sdk: Path, tmp_path: Path) -> None:
    proc = configure(tmp_path / "build", f"-DRACK_SDK_DIR={rack_sdk}", "-DCMAKE_BUILD_TYPE=Release")
    assert proc.returncode != 0
    assert "Leave CMAKE_BUILD_TYPE empty" in errors(proc)


@pytest.mark.parametrize(
    ("source", "clean"),
    [
        ("int Py_IsInitialized(void);\nint f(void) { return Py_IsInitialized(); }\n", False),
        ("int f(void) { return 1; }\n", True),
    ],
)
def test_audio_symbol_check_detects_python_references(tmp_path: Path, source: str, clean: bool) -> None:
    require_tools("gcc", "nm")
    (tmp_path / "kernel.c").write_text(source)
    obj = tmp_path / "kernel.o"
    subprocess.run(["gcc", "-c", "-o", str(obj), str(tmp_path / "kernel.c")], check=True)
    proc = subprocess.run(
        [str(BIN / "rackpy-dev"), "audio-symbols", str(obj)], capture_output=True, text=True, check=False
    )
    assert (proc.returncode == 0) is clean, proc.stdout + proc.stderr
    if not clean:
        assert "Py_IsInitialized" in errors(proc)
