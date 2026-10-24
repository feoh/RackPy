# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16A: Python models registered by a trusted entry module, in actual Rack.

The entry module adds its models while Rack loads the plugin. Rack itself then
creates their modules: through the browser's path, deletion and undo, its exit
autosave and the reload at the next launch.
"""

import json
from collections.abc import Callable
from pathlib import Path
from typing import Any

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
SUITE = Path(__file__).resolve().parent / "in_rack"
ENTRY = Path(__file__).resolve().parent / "entries"


def manifest_modules(plugin: Path, slugs: list[str]) -> None:
    """Add module entries to the staged plugin.json, as a plugin author lists them."""
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"] += [{"slug": s, "name": f"{s} module", "description": "RP-16A test", "tags": []} for s in slugs]
    (plugin / "plugin.json").write_text(json.dumps(manifest))


def stage_entry(bundle: Path, user: Path, entry: str, config: dict[str, Any], slugs: list[str]) -> Path:
    plugin = stage_plugin(
        bundle,
        user,
        entry=entry,
        files={
            f"pythonpath/{entry}.py": (ENTRY / f"{entry}.py").read_text(),
            "pythonpath/rp16_config.json": json.dumps(config),
        },
    )
    manifest_modules(plugin, slugs)
    return plugin


@pytest.mark.gui
def test_plugin_api_in_live_ui(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()


@pytest.mark.gui
def test_rack_creates_removes_restores_and_reloads_python_modules(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    slugs = ["PyAlpha", "PyBeta", "PyBroken"]
    first = tmp_path / "first.json"
    stage_entry(make_hooks_bundle(), tmp_path, "rp16_models", {"phase": 1, "result": str(first)}, slugs)
    write_patch(tmp_path, [PROBE_MODULE])
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0, run.tail()
    assert not run.lines("Fatal signal"), run.tail()
    one = json.loads(first.read_text())
    assert "error" not in one, one.get("error")
    assert one["staged"] == slugs  # the test hooks add their native model after the entry module
    ids = one["ids"]
    assert one["models"] == {s: [s, False] for s in slugs}
    assert one["preview"] == [6 * 15, 380, ["PyAlpha module"], False, True]  # 6 HP, Rack's 380 px, plugin.json name
    assert one["beta_after_remove"] is False and one["beta_after_undo"] is True
    assert one["beta_value_after_undo"] == -0.25  # undo restores the state Rack saved on deletion
    assert one["created"] == ["PyAlpha", "PyBeta", "PyBroken", "PyBeta"]

    # Rack autosaves when it exits; that patch is what the next launch reopens.
    saved = {m["id"]: m for m in json.loads((tmp_path / "autosave" / "patch.json").read_text())["modules"]}
    assert saved[ids["PyAlpha"]]["data"] == {"schema": 1, "notes": ["hello", "λ"]}
    assert {p["id"]: p["value"] for p in saved[ids["PyAlpha"]]["params"]} == {0: 7.5, 1: 0}
    assert saved[ids["PyBroken"]]["data"] == {"kept": True}

    second = tmp_path / "second.json"
    (tmp_path / "plugins-lin-x64" / "RackPy" / "pythonpath" / "rp16_config.json").write_text(
        json.dumps({"phase": 2, "result": str(second), "ids": ids, "broken": True})
    )
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0, run.tail()
    assert not run.lines("Fatal signal"), run.tail()
    two = json.loads(second.read_text())
    assert "error" not in two, two.get("error")
    assert two["alpha"] == [7.5, {"schema": 1, "notes": ["hello", "λ"]}]
    assert two["beta"] == -0.25
    assert two["broken"] == ["PyBroken", True]  # its factory raised: Rack got a placeholder
    assert two["diagnosed"] and run.lines("rp16-intentional-factory-error")
    assert run.lines("model PyBroken could not create its module")
    # The placeholder wrote the module's saved state back unchanged.
    reloaded = {m["id"]: m for m in json.loads((tmp_path / "autosave" / "patch.json").read_text())["modules"]}
    assert reloaded[ids["PyBroken"]]["data"] == {"kept": True}
    assert reloaded[ids["PyBroken"]]["params"] == saved[ids["PyBroken"]]["params"]


def test_mismatched_manifest_adds_nothing_and_rack_continues(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    stage_entry(make_bundle(), tmp_path, "rp16_mismatch", {}, ["PyAlpha", "PyMissing"])
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    failed = run.lines("RackPy host initialization failed (4)")
    assert failed, run.tail()
    assert "listed but not added: PyMissing" in failed[0] and "added but not listed: PyExtra" in failed[0]
    assert run.lines("Could not load plugin") and not run.lines("Loaded plugin RackPy")


def test_registration_errors_leave_no_partial_registry(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    stage_entry(make_bundle(), tmp_path, "rp16_errors", {}, ["PyAlpha", "PyTwice"])
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    report = run.lines("rp16-errors ")
    assert report, run.tail()
    errors = json.loads(report[0].split("rp16-errors ", 1)[1])
    assert errors == {
        "duplicate": "ValueError",
        "invalid": "ValueError",
        "empty": "ValueError",
        "not_callable": "TypeError",
        "added_twice": "OwnershipError",
        "models": ["PyAlpha", "PyTwice"],
    }
    assert run.lines("Loaded plugin RackPy")


def test_a_failing_entry_module_adds_nothing(make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    stage_entry(make_bundle(), tmp_path, "rp16_raises", {}, ["PyAlpha"])
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    assert run.lines("RackPy host initialization failed (3)") and run.lines("rp16-entry-intentional-error")
    assert run.lines("Could not load plugin") and not run.lines("Loaded plugin RackPy")
