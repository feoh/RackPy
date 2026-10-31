# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24C: rack::history from Python, in actual Rack."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, rack_platforms, run_pytest_in_rack, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, rack_platforms()]
HERE = Path(__file__).resolve().parent
SUITE = HERE / "in_rack"
PYTONE = {"plugin": "RackPy", "model": "PyTone", "version": "2.0.0", "params": [{"id": 0, "value": 2}]}


def test_history_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()


def test_history_through_racks_ui_paths(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result_path = tmp_path / "result.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp24c_history",
        files={
            "pythonpath/rp24c_history.py": (HERE / "entries" / "rp24c_history.py").read_text(),
            "pythonpath/rp24c_config.json": json.dumps({"result": str(result_path)}),
        },
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "PyTone", "name": "Tone", "description": "RP-24C test", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    write_patch(tmp_path, [PROBE_MODULE])
    patch_path = tmp_path / "autosave" / "patch.json"
    patch = json.loads(patch_path.read_text())
    patch["modules"][0]["pos"] = [0, 0]
    patch["modules"] += [{**PYTONE, "id": 2, "pos": [20, 0]}, {**PYTONE, "id": 3, "pos": [30, 0]}]
    patch["cables"] = [{"id": 7, "outputModuleId": 2, "outputId": 0, "inputModuleId": 3, "inputId": 0}]
    patch_path.write_text(json.dumps(patch))
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]
    assert result["start_cables"] == 11  # one cable widget, one engine cable
    assert result["bypass"] == [True, "rack::history::ModuleBypass", 2, False, True, True]
    assert result["disconnect"] == [0, "rack::history::ComplexAction", ["rack::history::CableRemove"], 11]
    assert result["append"] == [1, "rack::history::CableRemove", 0, 11, True]  # Rack removes it; undo restores it
    assert result["cable_color"] == [True] * 5
    assert result["cable_add"] == [7, 2, 3, True]
    assert result["cable_remove"] == [0, True, 11, True]
    assert result["bad_cable"] == 11  # Rack would index input 4 and leave an engine cable behind
    assert result["drag"] == [["rack::history::ModuleMove"], True, True, True]
    assert result["move"] is True
    assert result["removal"] == [True, True, True, True, True, True]
