# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-10 in a real Rack: context binding, handles, threads, ownership, diagnostics and teardown.

Uses a test-hooks build (RuntimeProbe module, _rackpy_host._testing) that is
never shipped. The windowed scenario (scenarios/runtime_checks.py) runs inside
Rack under Xvfb and logs one CHECK line per rule; headless runs cover the
teardown paths.
"""

import re
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import (
    RackRun,
    assert_clean_exit,
    linux_x64_only,
    run_rack,
    run_rack_windowed,
    stage_plugin,
    write_patch,
)

pytestmark = [pytest.mark.host, linux_x64_only()]
SCENARIOS = Path(__file__).resolve().parent / "scenarios"
PROBE = {"id": 1, "plugin": "RackPy", "model": "RuntimeProbe", "version": "2.0.0", "params": [{"id": 0, "value": 0.5}]}
CHECK = re.compile(r"RackPy: CHECK (?P<name>.+?) (?P<result>PASS|FAIL) ?(?P<detail>.*)$")


def checks(run: RackRun) -> dict[str, tuple[str, str]]:
    found = {}
    for line in run.log.splitlines():
        if m := CHECK.search(line):
            found[m["name"]] = (m["result"], m["detail"])
    return found


def stage_scenario(bundle: Path, user: Path, name: str) -> Path:
    source = (SCENARIOS / f"{name}.py").read_text()
    return stage_plugin(bundle, user, entry=name, files={f"pythonpath/{name}.py": source})


@pytest.fixture(scope="module")
def windowed(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path_factory: pytest.TempPathFactory
) -> RackRun:
    user = tmp_path_factory.mktemp("windowed")
    stage_scenario(make_hooks_bundle(), user, "runtime_checks")
    write_patch(user, [PROBE])
    return run_rack_windowed(rack_host, user)


EXPECTED = [
    "thread role on the UI thread",
    "context bound",
    "identity across frames",
    "audio keeps processing",
    "added module is alive",
    "added module processes",
    "deleted module handle is not alive",
    "stale handle raises StaleHandleError",
    "StaleHandleError is a ReferenceError",
    "1000 cycles: every deleted handle is stale",
    "1000 cycles: identity registry does not grow",
    "1000 cycles: native references do not grow",
    "1000 cycles: Python objects do not grow",
    "1000 cycles: RSS grows less than 4 MiB",
    "UI-only call from a Python thread raises ThreadError",
    "engine call from a Python thread works",
    "Python thread role is other",
    "handle under a foreign context raises ContextError",
    "foreign context recorded",
    "first transfer succeeds",
    "second transfer raises OwnershipError",
    "reset callback got the same handle",
    "engine call inside onReset raises EngineLockError",
    "Python refused on an engine thread",
    "refusal recorded from a rack thread",
    "object held by the native holder",
    "object released on the UI thread after an engine-thread drop",
    "raising callback was caught",
    "raising callback was recorded",
    "diagnostics are bounded",
    "diagnostics keep the newest",
    "workers ran",
]


def test_windowed_scenario_completes_and_exits_cleanly(windowed: RackRun) -> None:
    assert windowed.returncode == 0, windowed.tail()
    assert windowed.lines("RackPy: SCENARIO DONE"), windowed.tail()
    assert not windowed.lines("Fatal signal"), windowed.tail()


@pytest.mark.parametrize("name", EXPECTED)
def test_runtime_rule_holds_in_rack(windowed: RackRun, name: str) -> None:
    found = checks(windowed)
    assert name in found, f"check did not run; log tail:\n{windowed.tail()}"
    result, detail = found[name]
    assert result == "PASS", detail


def test_every_check_is_expected(windowed: RackRun) -> None:
    unexpected = sorted(set(checks(windowed)) - set(EXPECTED) - {"1000 cycles: RSS growth"})
    assert unexpected == []


def test_shutdown_runs_in_the_documented_order(windowed: RackRun) -> None:
    log = windowed.log
    order = [
        "Saving autosave",  # Rack saves the patch, with its Python module widgets, first
        "RackPy context shutdown begins (scene deletion)",
        "deliberate failure in a closing hook",
        "WORKER polite joined after",
        "RackPy context closed; handles to it are now stale",
        "WORKER careless saw StaleHandleError",
        "Deleting engine",
        "RackPy interpreter finalized: Py_FinalizeEx returned 0",
    ]
    positions = [log.find(marker) for marker in order]
    assert all(p >= 0 for p in positions), dict(zip(order, positions, strict=True))
    # The careless worker may notice the closed context before or after Rack deletes the engine,
    # but always after the context closed; every other step is strictly ordered.
    strict = [p for i, p in enumerate(positions) if order[i] != "WORKER careless saw StaleHandleError"]
    assert strict == sorted(strict), dict(zip(order, positions, strict=True))
    assert positions[4] > positions[3]


def test_rss_growth_is_reported(windowed: RackRun) -> None:
    found = checks(windowed)
    print(found.get("1000 cycles: RSS growth"))
    assert "1000 cycles: RSS growth" in found


def test_headless_teardown_uses_scene_deletion(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    stage_plugin(make_hooks_bundle(), tmp_path)
    write_patch(tmp_path, [PROBE])
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    assert run.lines("RackPy bound to Rack context"), run.tail()
    assert run.lines("RackPy context shutdown begins (scene deletion)"), run.tail()
    assert run.lines("Py_FinalizeEx returned 0"), run.tail()


def test_leaked_native_reference_refuses_finalization(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    entry = "from _rackpy_host import _testing\n_testing.leak_reference(object())\n"
    stage_plugin(make_hooks_bundle(), tmp_path, entry="leaky", files={"pythonpath/leaky.py": entry})
    run = run_rack(rack_host, tmp_path, settle=0.3)
    assert_clean_exit(run)
    (refused,) = run.lines("RackPy did not finalize the interpreter")
    assert "1 Python reference(s) are still held by native objects" in refused
    assert not run.lines("Py_FinalizeEx")


def test_closing_hook_without_a_bound_context_runs_at_destroy(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    entry = (
        "import _rackpy_host\nfrom rackpy import runtime\n"
        "runtime.on_context_closing(lambda: _rackpy_host.log_info('closing hook ran'))\n"
    )
    stage_plugin(make_hooks_bundle(), tmp_path, entry="hooked", files={"pythonpath/hooked.py": entry})
    run = run_rack(rack_host, tmp_path, settle=0.3)  # no RackPy module: no context is ever bound
    assert_clean_exit(run)
    assert not run.lines("RackPy bound to Rack context")
    assert run.lines("RackPy: closing hook ran"), run.tail()
    assert run.lines("Py_FinalizeEx returned 0")


def test_repeated_host_load_and_unload(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    """Rack loads a plugin once per process, so a reload is a new Rack process; do it five times."""
    bundle = make_hooks_bundle()
    for i in range(5):
        user = tmp_path / f"run{i}"
        stage_plugin(bundle, user)
        write_patch(user, [PROBE])
        run = run_rack(rack_host, user, settle=0.3)
        assert_clean_exit(run)
        assert run.lines("Py_FinalizeEx returned 0"), run.tail()
