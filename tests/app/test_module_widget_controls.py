# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28A: Python controls in a Python model's ModuleWidget, deleted and undone through Rack's own code.

The module widget is Rack's, from the module browser's code path. Its Python
controls are added as a ModuleWidget's constructor adds them. Rack deletes
the widget with its Delete action and recreates it with undo.
"""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, rack_platforms, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, rack_platforms()]
HERE = Path(__file__).resolve().parent


def test_python_controls_in_a_module_widget_survive_rack_delete_and_undo(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result_path = tmp_path / "result.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp28a_controls",
        files={
            "pythonpath/rp28a_controls.py": (HERE / "entries" / "rp28a_controls.py").read_text(),
            "pythonpath/rp28a_config.json": json.dumps({"result": str(result_path)}),
        },
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "PyJacks", "name": "Jacks", "description": "RP-28A test", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    write_patch(tmp_path, [PROBE_MODULE])
    # Rack's first-launch tip window would sit over the rack and take the clicks.
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]
    assert result["touched_is_knob"]
    before, after = result["cables"]
    assert before == after  # ad-hoc Python ports make no cables (Rack's undo could not restore them)
    removed = result["after_remove"]
    assert removed == {"controls_alive": [False, False, False, False], "touched": True, "module": False}
    assert result["after_undo"] is True
