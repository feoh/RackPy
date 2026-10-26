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


def test_python_modules_into_the_engine_and_back(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result_path = tmp_path / "result.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp24a_engine",
        files={
            "pythonpath/rp24a_engine.py": (HERE / "entries" / "rp24a_engine.py").read_text(),
            "pythonpath/rp24a_config.json": json.dumps({"result": str(result_path)}),
        },
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "PyTone", "name": "Tone", "description": "RP-24A test", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    write_patch(tmp_path, [PROBE_MODULE])
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]
    assert result["added"] == [True, True, True, "PyTone"]
    assert result["again"].startswith("refused") and "already has" in result["again"]
    assert result["with_control"].startswith("refused") and "controls" in result["with_control"]
    assert "would deadlock" in result["reentry"], result["reentry"]
    assert result["reset"] == 2  # Rack's reset to the default
    assert result["stepped"] == [96, 32, True]  # the master module alone steps the engine
    assert result["removed"] == [False, True, True] and result["closed"] is False
    assert "widget owns it" in result["widget_owned"]
