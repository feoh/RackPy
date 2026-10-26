# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28E: Python cable widgets in actual Rack, between two Python modules' factory ports."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, linux_x64_only, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent
PYTONE = {"plugin": "RackPy", "model": "PyTone", "version": "2.0.0"}


def test_python_cable_widgets_through_racks_rack_engine_and_history(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result_path = tmp_path / "result.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp28e_cables",
        files={
            "pythonpath/rp28e_cables.py": (HERE / "entries" / "rp28e_cables.py").read_text(),
            "pythonpath/rp28e_config.json": json.dumps({"result": str(result_path)}),
            "pythonpath/cable_widget.py": (HERE.parents[1] / "docs" / "snippets" / "cable_widget.py").read_text(),
        },
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "PyTone", "name": "Tone", "description": "RP-28E test", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    write_patch(tmp_path, [PROBE_MODULE])
    patch_path = tmp_path / "autosave" / "patch.json"
    patch = json.loads(patch_path.read_text())
    patch["modules"][0]["pos"] = [0, 0]
    patch["modules"] += [{**PYTONE, "id": i, "pos": [10 * i, 0]} for i in (2, 3, 4)]
    patch_path.write_text(json.dumps(patch))
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]
    assert result["plugs"] == ["HostPlugWidget", True]
    assert result["wrong_end"].startswith("ValueError") and "input port" in result["wrong_end"]
    assert result["detached_update"].startswith("OwnershipError") and "rack" in result["detached_update"]
    assert result["in_rack"] == [True, True, 0]  # the rack has it, incomplete in the engine until update_cable
    assert result["cabled"] == [True, 1, True]
    assert result["json"] == {"color": "#2878dc"}
    assert result["listed"] == [True, True]
    assert result["undone"] == [False, 0]  # Rack's undo deleted the Python cable widget and its cable
    assert result["redone"] == ["HostCableWidget", 1]  # Rack's redo made its own
    assert "Rack's own cable widgets" in result["host_remove"]
    assert result["adopted"] == [True, True, True, 1]
    assert result["released"] == [True, 0, True]  # out of the engine too
    assert result["removed"] == [True, True, True, 0]
    assert result["dead_port"] is True
    assert result["snippet"] == [True, True, 1, 0]
