# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28B, the actual-host gate: Python module widgets through Rack's module browser, cables, history and reload.

Windowed Rack runs a Python model whose widget is a Python ModuleWidget, with
original SVG artwork:

1. Rack's module browser builds its preview.
2. A click in the browser adds the module.
3. Rack's cable dragging connects two such modules.
4. Rack's Delete, undo and redo rebuild the widget with the Python factory.
5. Rack reloads the patch it saved as it quit, cables included.
"""

import json
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from tests.support import PROBE_MODULE, linux_x64_only, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent
DOCS = HERE.parents[1] / "docs"
PANEL = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="90" height="380">'
    '<rect width="90" height="380" fill="#2a3340"/><rect x="8" y="8" width="74" height="20" fill="#3f4b5c"/></svg>'
)
JACK = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24">'
    '<circle cx="12" cy="12" r="11" fill="#b0b8c4"/><circle cx="12" cy="12" r="5" fill="#101418"/></svg>'
)


def launch(rack: Path, user: Path, plugin: Path, phase: int) -> dict[str, Any]:
    result = user / f"result{phase}.json"
    (plugin / "pythonpath" / "rp28b_config.json").write_text(json.dumps({"phase": phase, "result": str(result)}))
    run = run_rack_windowed(rack, user, timeout=180)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    data: dict[str, Any] = json.loads(result.read_text())
    assert "error" not in data, data["error"]
    return data


def test_python_module_widgets_through_racks_browser_cables_history_and_reload(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp28b_browser",
        files={
            "pythonpath/rp28b_browser.py": (HERE / "entries" / "rp28b_browser.py").read_text(),
            "res/voice.svg": PANEL,
            "res/jack.svg": JACK,
            "pythonpath/module_widget.py": (DOCS / "snippets" / "module_widget.py").read_text(),
        },
    )
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"] += [
        {"slug": "PyVoice", "name": "Voice", "description": "RP-28B test", "tags": []},
        {"slug": "PyBlink", "name": "Blink", "description": "the documentation's example", "tags": []},
    ]
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    write_patch(tmp_path, [PROBE_MODULE])
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))

    one = launch(rack_host, tmp_path, plugin, 1)
    assert one["preview_seen"] and one["preview"] == [True, True]  # the browser's preview: no module
    assert one["added_from_browser"]
    assert [True, True, True] in one["deprecated_constructor"]  # preview
    assert [False, True, True] in one["deprecated_constructor"]  # module-owned widget
    assert all(one["hovered_output"])  # input-first drag, while both ports are unconnected
    assert one["clear_cables"] == [[1, 0, 1], [0, 0, 0], True]
    assert all(one["hovered_cable"])  # live incomplete cable, hovered input and output plug
    assert one["cable"] == [1, 0, 1] and one["connected"]  # a cable between two Python modules' factory ports
    views = one["cable_views"]  # RP-28D: Rack's cable through its host views
    assert views["views"] == ["HostCableWidget", "HostPlugWidget"] and views["ports"] == [True, True, True]
    assert all(views["plug"]) and all(views["lookups"]) and views["positions"] and views["json"]
    assert views["color"] == "#3060f0"
    first = one["removed_id"]
    assert one["removed"] == {"removes": [f"module-{first}"], "widget": False, "cables": [0, 0, 0]}
    assert one["undone"] == {"rebuilt": True, "cables": [1, 0, 1]}  # the factory rebuilt it; Rack restored the cable
    assert one["redone"] == [0, 0, 0]
    assert one["create_module"] == [True, True]
    assert one["preview_call"] == [True, True]
    assert one["second_owner"] == "refused" and one["outside_factory"] == "refused"
    assert one["owner_module"] and one["owner_closed"] is False
    assert one["rack_add_remove"] == [True, True, True, True, True]  # into the rack and back to Python
    actions = one["actions"]
    assert actions["to_json"] == ["PyVoice", "RackPy"] and actions["from_json"] == 5.0
    assert actions["preset"] == [True, 5.0] and actions["template"] and actions["template_loaded"] == 5.0
    assert actions["template_cleared"] and actions["paste"] == [True, 5.0] and actions["paste_json"]
    assert actions["reset"] == 5.0 and actions["bypassed"] and actions["cloned"]
    # The clone copies the cable into its input too (Rack's Duplicate with cables): two cables, then none.
    assert actions["disconnected"][2] < actions["reconnected"][2]
    assert actions["plain_disconnect"][0][2] < actions["reconnected"][2]
    assert actions["plain_disconnect"][1]  # no history action, unlike disconnect_action
    assert one["blink"] == [True, ["PyBlink", False], True]  # the documentation's module widget, in Rack

    two = launch(rack_host, tmp_path, plugin, 2)
    assert sorted(two["reloaded"]["rebuilt"]) == sorted(one["ids"])  # Rack's patch loading used the factory
    assert two["reloaded"]["cables"] == one["final_cables"]  # and found the cables' ports on the rebuilt widgets
