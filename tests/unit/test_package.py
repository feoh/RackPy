# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the pure-Python parts of rackpy, outside Rack.

These use a stand-in for the embedded _rackpy_host module. They check
rackpy's own logic only; tests/integration/test_host_smoke.py is what shows
the package working inside Rack.
"""

import importlib
import sys
import tomllib
import types
from collections.abc import Iterator
from pathlib import Path

import pytest

import rackpy

pytestmark = pytest.mark.unit
REPO = Path(__file__).resolve().parents[2]


def test_version_matches_pyproject() -> None:
    project = tomllib.loads((REPO / "pyproject.toml").read_text())["project"]
    assert rackpy.__version__ == project["version"]


def test_package_exports_nothing_but_the_version() -> None:
    # No Rack API is bound yet; anything public here would be an unbacked claim. The submodules
    # rackpy.errors and rackpy.runtime are RackPy's own runtime API, not Rack bindings.
    assert rackpy.__all__ == ["__version__"]
    names = [n for n, v in vars(rackpy).items() if not n.startswith("_") and not isinstance(v, types.ModuleType)]
    assert names == []


def test_bootstrap_outside_rack_names_the_reason(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delitem(sys.modules, "rackpy._bootstrap", raising=False)
    monkeypatch.setitem(sys.modules, "_rackpy_host", None)  # makes the import fail
    with pytest.raises(ImportError, match="only inside the RackPy host"):
        importlib.import_module("rackpy._bootstrap")


class FakeHost(types.ModuleType):
    def __init__(self, abi: int) -> None:
        super().__init__("_rackpy_host")
        self.HOST_ABI = abi
        self.BINDING = "pybind11 3.1.0"
        self.lines: list[str] = []

    def rack_version(self) -> str:
        return "2.6.6"

    def log_info(self, message: str) -> None:
        self.lines.append(message)


@pytest.fixture
def fake_host(monkeypatch: pytest.MonkeyPatch) -> Iterator[FakeHost]:
    host = FakeHost(abi=1)
    monkeypatch.setitem(sys.modules, "_rackpy_host", host)
    monkeypatch.delitem(sys.modules, "rackpy._bootstrap", raising=False)
    yield host
    sys.modules.pop("rackpy._bootstrap", None)


def test_start_logs_runtime_facts(fake_host: FakeHost) -> None:
    bootstrap = importlib.import_module("rackpy._bootstrap")
    bootstrap.start("/plugins/RackPy/", "")
    (line,) = fake_host.lines
    assert line.startswith(f"rackpy {rackpy.__version__}, Python ")
    assert line.endswith("pybind11 3.1.0, Rack 2.6.6, plugin /plugins/RackPy/")


def test_start_refuses_a_host_with_another_abi(fake_host: FakeHost) -> None:
    fake_host.HOST_ABI = 2
    bootstrap = importlib.import_module("rackpy._bootstrap")
    with pytest.raises(RuntimeError, match="needs host ABI 1, but the host provides 2"):
        bootstrap.start("/plugins/RackPy/", "")
    assert not fake_host.lines


def test_start_imports_the_entry_module(fake_host: FakeHost, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tmp_path / "entry_for_test.py").write_text("import _rackpy_host\n_rackpy_host.log_info('entry ran')\n")
    monkeypatch.syspath_prepend(str(tmp_path))
    monkeypatch.delitem(sys.modules, "entry_for_test", raising=False)
    importlib.import_module("rackpy._bootstrap").start("/p/", "entry_for_test")
    assert fake_host.lines[-1] == "entry ran"
