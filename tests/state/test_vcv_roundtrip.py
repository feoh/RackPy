# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-15D: a Python module's state and the plugin's settings through Rack's real .vcv save and open.

Rack opens a .vcv given on its command line (File > Open's code path) and the
test presses Ctrl+S through Rack's input dispatcher, so Rack's own patch
manager writes the .vcv archive, patch storage included. Rack writes
settings.json as it exits, and reads the plugin's part back at the next launch.
"""

import json
import shutil
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from tests.support import PROBE_MODULE, linux_x64_only, run_pytest_in_rack, run_rack_windowed, stage_plugin

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent
ZSTD_MAGIC = bytes.fromhex("28b52ffd")


def configure(plugin: Path, config: dict[str, Any]) -> None:
    (plugin / "pythonpath" / "rp15d_config.json").write_text(json.dumps(config))


def test_state_and_settings_survive_rack_save_and_reopen(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    first, second = tmp_path / "first.json", tmp_path / "second.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp15d_state",
        files={"pythonpath/rp15d_state.py": (HERE / "entries" / "rp15d_state.py").read_text()},
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "PyStore", "name": "Store", "tags": []})
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    configure(plugin, {"phase": 1, "result": str(first)})
    vcv = tmp_path / "patch.vcv"
    # A plain-JSON patch, which Rack still opens; its first Save rewrites it as Rack's archive.
    vcv.write_text(json.dumps({"version": "2.6.6", "modules": [PROBE_MODULE], "cables": []}))

    run = run_rack_windowed(rack_host, tmp_path, patch=vcv)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    one = json.loads(first.read_text())
    assert "error" not in one, one.get("error")
    assert "settings_loaded" not in one  # nothing stored yet, so Rack did not call settingsFromJson
    assert one["before"] == [0.25, {"notes": ["kept", "λ"]}, "1 2 3"]
    assert one["deleted"] is False and one["after_undo"] == one["before"]  # undo restored the module and its files
    assert vcv.read_bytes()[:4] == ZSTD_MAGIC  # Rack's own .vcv archive
    settings = json.loads((tmp_path / "settings.json").read_text())
    assert settings["pluginSettings"]["RackPy"] == {"schema": 1, "launches": 1}

    shutil.rmtree(tmp_path / "autosave")  # the next launch can restore only from the .vcv
    configure(plugin, {"phase": 2, "result": str(second), "id": one["id"]})
    run = run_rack_windowed(rack_host, tmp_path, patch=vcv)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    two = json.loads(second.read_text())
    assert "error" not in two, two.get("error")
    assert two["settings_loaded"] == {"schema": 1, "launches": 1}
    assert two["reopened"] == one["before"]  # parameters, custom data and patch storage from the archive
    settings = json.loads((tmp_path / "settings.json").read_text())
    assert settings["pluginSettings"]["RackPy"] == {"schema": 1, "launches": 2}


def test_settings_handlers_in_live_ui(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, HERE / "in_rack", during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
