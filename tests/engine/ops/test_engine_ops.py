# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24A: Rack's engine from Python, in actual Rack."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, linux_x64_only, run_pytest_in_rack, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent
SUITE = HERE / "in_rack"


def test_engine_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()


PYTONE = {"plugin": "RackPy", "model": "PyTone", "version": "2.0.0", "params": [{"id": 0, "value": 2}]}


def test_python_modules_cables_and_param_handles_in_racks_engine(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result_path = tmp_path / "result.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp24_engine",
        files={
            "pythonpath/rp24_engine.py": (HERE / "entries" / "rp24_engine.py").read_text(),
            "pythonpath/rp24_config.json": json.dumps({"result": str(result_path)}),
        },
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "PyTone", "name": "Tone", "description": "RP-24 test", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    write_patch(tmp_path, [PROBE_MODULE])
    patch_path = tmp_path / "autosave" / "patch.json"
    patch = json.loads(patch_path.read_text())
    patch["modules"][0]["pos"] = [0, 0]
    patch["modules"] += [{**PYTONE, "id": 2, "pos": [20, 0]}, {**PYTONE, "id": 3, "pos": [30, 0]}]
    patch["cables"] = [
        {"id": 7, "outputModuleId": 2, "outputId": 0, "inputModuleId": 3, "inputId": 0, "color": "#f3374b"}
    ]
    patch_path.write_text(json.dumps(patch))
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]
    # RP-24A
    assert result["added"] == [True, True, True, "PyTone"]
    assert result["again"].startswith("refused") and "already has" in result["again"]
    assert result["with_control"].startswith("refused") and "controls" in result["with_control"]
    assert "would deadlock" in result["reentry"], result["reentry"]
    assert result["reset"] == 2  # Rack's reset to the default
    assert result["stepped"] == [96, 32, True]  # the master module alone steps the engine
    assert result["removed"] == [False, True, True] and result["closed"] is False
    assert "widget owns it" in result["widget_owned"]
    # RP-24B: a cable between modules Python put into the engine
    assert result["cable"] == [True, True, True, True, True, True, True, True]
    assert "cables are still connected" in result["cabled_module"]
    assert "cables Python added" in result["cabled_widget"]
    assert "has a module widget" in result["to_widget"]
    assert result["patched"][:2] == [True, True] and "cable widget owns" in result["patched"][2]
    assert result["cable_removed"] == [False, True, True, False, True]
    assert result["cable_readded"] == [True, True]
    assert result["mapped"] == [True, True]
    assert result["no_lock"] == [True, True, True, True, True]  # inside a ResetEvent, under Rack's exclusive lock
    # Rack's rack, then its engine, cleared
    assert result["rack_cleared"] == [True, True, True, True]
    assert result["engine_cleared"] == [False, False, False, 0, 0]
    assert "does not have" in result["remove_after_clear"]
    assert result["handle_readded"] == [True, False]
    assert run.lines("RackPy context shutdown begins"), run.tail()
