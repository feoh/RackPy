# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22D: watchdog-bounded subprocess probes, never crash the outer pytest."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import (
    PROBE_MODULE,
    assert_clean_exit,
    linux_x64_only,
    run_pytest_in_rack,
    run_rack,
    run_rack_windowed,
    stage_plugin,
    write_patch,
)

pytestmark = [pytest.mark.host, linux_x64_only()]
HERE = Path(__file__).resolve().parent


@pytest.mark.parametrize(
    "case",
    ["override_exceptions", "recursion_through_super", "recursion_between_objects", "stale_after_close_and_delete"],
)
def test_laboratory_race_in_subprocess(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, case: str
) -> None:
    result = run_pytest_in_rack(make_bundle(), rack_host, tmp_path, HERE / "in_rack_races", args=["-k", case])
    assert result.exit == 0, result.summary()
    assert list(result.outcomes().values()) == ["passed"], result.summary()
    assert_clean_exit(result.run)
    assert result.run.lines("Py_FinalizeEx returned 0"), result.run.tail()


@pytest.mark.parametrize("live_context", [False, pytest.param(True, marks=pytest.mark.gui)])
@pytest.mark.parametrize("release_on_close", [False, True])
def test_shutdown_with_laboratory_objects_alive(
    make_bundle: Callable[[], Path],
    make_hooks_bundle: Callable[[], Path],
    rack_host: Path,
    tmp_path: Path,
    live_context: bool,
    release_on_close: bool,
) -> None:
    source = (
        (HERE / "scenarios" / "shutdown.py")
        .read_text()
        .replace("RELEASE_ON_CLOSE = False", f"RELEASE_ON_CLOSE = {release_on_close}")
    )
    bundle = make_hooks_bundle() if live_context else make_bundle()
    stage_plugin(bundle, tmp_path, entry="lab_shutdown", files={"pythonpath/lab_shutdown.py": source})
    if live_context:
        write_patch(tmp_path, [PROBE_MODULE])
    run = run_rack_windowed(rack_host, tmp_path) if live_context else run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    for marker in ("LAB SHUTDOWN armed", "LAB SHUTDOWN closing hook passed"):
        assert run.lines(marker), run.tail()
    assert not run.lines("on_context_closing callback"), run.tail()
    assert bool(run.lines("LAB SHUTDOWN output connected in live context")) == live_context, run.tail()
    disconnected = "LAB SHUTDOWN laboratory drivers disconnected without Python"  # RP-22F
    assert bool(run.lines(disconnected)) == live_context, run.tail()
    if release_on_close:
        assert run.lines("LAB SHUTDOWN holders released"), run.tail()
        assert run.lines("Py_FinalizeEx returned 0"), run.tail()
        assert not run.lines("RackPy did not finalize"), run.tail()
    else:
        assert run.lines("LAB SHUTDOWN holders retained"), run.tail()
        assert run.lines("RackPy did not finalize the interpreter"), run.tail()
        assert run.lines("Python reference(s) are still held by native objects"), run.tail()
        assert not run.lines("Py_FinalizeEx"), run.tail()
