# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.runtime and the bootstrap hooks, with a stand-in for the embedded _rackpy_host module.

These check rackpy's own logic; tests/runtime checks the same API inside Rack.
"""

import importlib
import sys
import types
from collections.abc import Iterator
from typing import Any

import pytest

pytestmark = pytest.mark.unit


class FakeHost(types.ModuleType):
    HOST_ABI = 2
    BINDING = "pybind11 3.1.0"
    DIAGNOSTIC_CAPACITY = 256
    TEST_HOOKS = False

    def __init__(self) -> None:
        super().__init__("_rackpy_host")
        self.infos: list[str] = []
        self.warnings: list[str] = []
        self.role = "main"
        self.records: list[tuple[int, float, str, str, str]] = []

    def rack_version(self) -> str:
        return "2.6.6"

    def log_info(self, message: str) -> None:
        self.infos.append(message)

    def log_warning(self, message: str) -> None:
        self.warnings.append(message)

    def thread_role(self) -> str:
        return self.role

    def diagnostics(self, after: int = 0) -> list[tuple[int, float, str, str, str]]:
        return [r for r in self.records if r[0] > after]


@pytest.fixture
def host(monkeypatch: pytest.MonkeyPatch) -> Iterator[FakeHost]:
    fake = FakeHost()
    monkeypatch.setitem(sys.modules, "_rackpy_host", fake)
    for name in ("rackpy.runtime", "rackpy._bootstrap"):
        monkeypatch.delitem(sys.modules, name, raising=False)
    yield fake
    for name in ("rackpy.runtime", "rackpy._bootstrap"):
        sys.modules.pop(name, None)


def runtime() -> Any:
    return importlib.import_module("rackpy.runtime")


def test_runtime_outside_rack_names_the_reason(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delitem(sys.modules, "rackpy.runtime", raising=False)
    monkeypatch.setitem(sys.modules, "_rackpy_host", None)
    with pytest.raises(ImportError, match="only inside Rack"):
        importlib.import_module("rackpy.runtime")


def test_diagnostics_become_records(host: FakeHost) -> None:
    host.records = [(1, 0.5, "callback_raised", "main", "boom"), (2, 0.6, "refused_rack_thread", "rack", "process")]
    rt = runtime()
    all_records = rt.diagnostics()
    assert [d.sequence for d in all_records] == [1, 2]
    assert all_records[1] == rt.Diagnostic(2, 0.6, "refused_rack_thread", "rack", "process")
    assert [d.sequence for d in rt.diagnostics(1)] == [2]


def test_thread_role(host: FakeHost) -> None:
    rt = runtime()
    assert rt.thread_role() == "main" and rt.is_main_thread()
    host.role = "other"
    assert rt.thread_role() == "other" and not rt.is_main_thread()


def test_closing_callbacks_run_newest_first_and_survive_failures(host: FakeHost) -> None:
    rt = runtime()
    ran: list[str] = []
    rt.on_context_closing(lambda: ran.append("first"))

    def failing() -> None:
        ran.append("failing")
        raise RuntimeError("deliberate")

    assert rt.on_context_closing(failing) is failing  # so it also works as a decorator
    rt.on_context_closing(lambda: ran.append("last"))
    importlib.import_module("rackpy._bootstrap").context_closing()
    assert ran == ["last", "failing", "first"]
    assert len(host.warnings) == 1 and "RuntimeError: deliberate" in host.warnings[0]
    # Each callback runs once: stop() after context_closing() finds nothing left.
    importlib.import_module("rackpy._bootstrap").stop()
    assert ran == ["last", "failing", "first"]


def test_stop_runs_closing_callbacks_when_no_context_was_bound(host: FakeHost) -> None:
    rt = runtime()
    ran: list[str] = []
    rt.on_context_closing(lambda: ran.append("ran"))
    importlib.import_module("rackpy._bootstrap").stop()
    assert ran == ["ran"]
