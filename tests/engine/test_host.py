# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24: the rackpy.engine.host facade in actual Rack."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, linux_x64_only, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent


def test_the_host_facade(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result_path = tmp_path / "result.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="host_facade",
        files={
            "pythonpath/host_facade.py": (HERE / "entries" / "host_facade.py").read_text(),
            "pythonpath/host_config.json": json.dumps({"result": str(result_path)}),
        },
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "PyTone", "name": "Tone", "description": "RP-24 test", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    write_patch(tmp_path, [PROBE_MODULE])
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]
    assert result["startup"] == "refused"
    assert result["modules"] == [True, True]
    assert result["connect"] == [True, True]
    assert "another plugin's" in result["connect_refused"]
    assert result["disconnect"] == [False, False]
    assert result["map"] == [True, "Level"]
    assert result["map_rollback"] is True
    assert result["unmap"] is True
    assert result["step"] == [True, "set levels", 7.0, 2.0]
    assert result["rollback"] == [2.0, True, "set levels"]  # undone, and nothing pushed
