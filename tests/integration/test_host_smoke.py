# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-08 actual-host smoke test: the production skeleton loads into Rack Free 2.6.6 and shuts down cleanly.

Builds the skeleton with bin/rackpy-dev, stages it in a temporary Rack user
directory and runs the pinned Rack Free host headless (Linux x86_64). An import
of the Python package outside Rack proves nothing about this; see tests/unit.

    uv run pytest tests/integration/test_host_smoke.py -v
"""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

import rackpy
from tests.support import assert_clean_exit, linux_x64_only, run_rack, stage_plugin

pytestmark = [pytest.mark.host, linux_x64_only()]

ENTRY = """
import json, sys
import _rackpy_host
import rackpy

for name in ("json", "decimal", "ssl", "sqlite3", "ctypes", "_dbm"):
    __import__(name)
try:
    import hijack  # only reachable through PYTHONPATH or the working directory
    hijacked = True
except ImportError:
    hijacked = False
facts = {
    "version": sys.version.split()[0],
    "isolated": sys.flags.isolated,
    "no_site": sys.flags.no_site,
    "ignore_environment": sys.flags.ignore_environment,
    "dont_write_bytecode": sys.flags.dont_write_bytecode,
    "path": sys.path,
    "prefix": sys.prefix,
    "rackpy_file": rackpy.__file__,
    "hijacked": hijacked,
    "test_hooks": _rackpy_host.TEST_HOOKS,
    "has_testing": hasattr(_rackpy_host, "_testing"),
}
_rackpy_host.log_info("smoke facts " + json.dumps(facts))
"""


@pytest.fixture
def bundle(make_bundle: Callable[[], Path]) -> Path:
    return make_bundle()


def facts(log_line: str) -> dict[str, object]:
    data: dict[str, object] = json.loads(log_line.split("smoke facts ", 1)[1])
    return data


def test_skeleton_loads_into_rack_and_finalizes(
    rack_host: Path, bundle: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    hijack = tmp_path / "hijack"
    hijack.mkdir()
    (hijack / "hijack.py").write_text("")
    monkeypatch.setenv("PYTHONPATH", str(hijack))
    user = tmp_path / "user"
    plugin = stage_plugin(bundle, user, entry="smoke_entry", files={"pythonpath/smoke_entry.py": ENTRY})

    run = run_rack(rack_host, user)

    assert_clean_exit(run)
    assert run.lines("Loaded plugin RackPy 2.0.0"), run.tail()
    assert run.lines("RackPy interpreter started: Python 3.14.9"), run.tail()
    started = f"RackPy: rackpy {rackpy.__version__}, Python 3.14.9, pybind11 3.1.0, Rack 2.6.6, plugin {plugin}"
    assert run.lines(started), run.tail()
    (line,) = run.lines("smoke facts")
    seen = facts(line)
    assert seen["version"] == "3.14.9"
    assert seen["isolated"] == 1 and seen["no_site"] == 1 and seen["ignore_environment"] == 1
    assert seen["dont_write_bytecode"] == 1
    assert seen["test_hooks"] is False and seen["has_testing"] is False, "a production build must not have test hooks"
    assert seen["hijacked"] is False, "PYTHONPATH or the working directory leaked into sys.path"
    path = seen["path"]
    assert isinstance(path, list) and path[0] == str(plugin / "pythonpath")
    assert all(p.startswith(str(plugin)) for p in path), path
    assert seen["prefix"] == str(plugin / "python")
    assert seen["rackpy_file"] == str(plugin / "pythonpath" / "rackpy" / "__init__.py")
    assert run.lines("RackPy host rackpy_host.so ready")
    assert run.lines("RackPy interpreter finalized: Py_FinalizeEx returned 0"), run.tail()
    assert not run.lines("RackPy refused"), run.tail()
    assert not list(plugin.rglob("__pycache__")), "the host wrote bytecode into the installed plugin"


def test_skeleton_without_entry_module(rack_host: Path, bundle: Path, tmp_path: Path) -> None:
    stage_plugin(bundle, tmp_path)
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    assert run.lines("RackPy interpreter started"), run.tail()
    assert run.lines("Py_FinalizeEx returned 0"), run.tail()


def test_missing_runtime_is_refused_with_a_reason(rack_host: Path, bundle: Path, tmp_path: Path) -> None:
    plugin = stage_plugin(bundle, tmp_path)
    (plugin / "python").unlink()
    run = run_rack(rack_host, tmp_path, settle=0.3)
    assert_clean_exit(run)
    assert run.lines("RackPy refused to start: bundled Python runtime missing or incomplete"), run.tail()
    assert not run.lines("RackPy interpreter started")


def test_bad_loader_configuration_is_refused(rack_host: Path, bundle: Path, tmp_path: Path) -> None:
    plugin = stage_plugin(bundle, tmp_path)
    config = json.loads((plugin / "rackpy-loader.json").read_text())
    (plugin / "rackpy-loader.json").write_text(json.dumps({**config, "abi": config["abi"] + 1}))
    run = run_rack(rack_host, tmp_path, settle=0.3)
    assert_clean_exit(run)
    assert run.lines(f'"abi" must be {config["abi"]}'), run.tail()
    assert not run.lines("RackPy interpreter started")


def test_failing_entry_module_is_reported_and_finalized(rack_host: Path, bundle: Path, tmp_path: Path) -> None:
    files = {"pythonpath/broken_entry.py": "raise RuntimeError('deliberate smoke-test failure')\n"}
    stage_plugin(bundle, tmp_path, entry="broken_entry", files=files)
    run = run_rack(rack_host, tmp_path, settle=0.3)
    assert_clean_exit(run)
    (failed,) = run.lines("RackPy host initialization failed (3)")
    assert "deliberate smoke-test failure" in failed
    assert run.lines("Py_FinalizeEx returned 0"), run.tail()


def test_second_rackpy_plugin_is_refused(rack_host: Path, make_bundle: Callable[[], Path], tmp_path: Path) -> None:
    """One interpreter owner per process (ADR 002, decision 4)."""
    stage_plugin(make_bundle(), tmp_path)
    second = make_bundle().rename(tmp_path / "RackPyB")
    manifest = json.loads((second / "plugin.json").read_text())
    (second / "plugin.json").write_text(json.dumps({**manifest, "slug": "RackPyB", "name": "RackPy B"}))
    stage_plugin(second, tmp_path)
    run = run_rack(rack_host, tmp_path, settle=0.3)
    assert_clean_exit(run)
    assert len(run.lines("RackPy interpreter started")) == 1, run.tail()
    assert len(run.lines("a CPython runtime is already loaded in this process")) == 1, run.tail()
