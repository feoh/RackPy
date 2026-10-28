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
import subprocess
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


@pytest.mark.parametrize("dialog_stub", [False, True], ids=["native", "dialog-stub"])
def test_python_module_widgets_through_racks_browser_cables_history_and_reload(
    make_hooks_bundle: Callable[[], Path],
    rack_host: Path,
    rack_sdk: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    dialog_stub: bool,
) -> None:
    if dialog_stub:
        shim = tmp_path / "dialog_stub.so"
        subprocess.run(
            [
                "cc",
                "-shared",
                "-fPIC",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(rack_sdk / "dep/include"),
                str(HERE / "dialog_stub.c"),
                "-o",
                str(shim),
            ],
            check=True,
        )
        monkeypatch.setenv("LD_PRELOAD", str(shim))
        monkeypatch.setenv("RACKPY_DIALOG_STUB", "1")
        monkeypatch.setenv("RACKPY_DIALOG_LOG", str(tmp_path / "dialogs.log"))
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
    assert all(one["incomplete_lookup"])  # singular deprecated lookup finds the actual incomplete cable
    assert all(one["direct_port_drag"])  # leave, enter, and drop called through Python
    assert all(one["direct_module_drag"])  # only left drag moves the module
    assert one["cable"] == [1, 0, 1] and one["connected"]  # two Python modules' factory ports
    assert one["cable_removals"] == [
        [0, 0, 0],
        [1, 0, 1],
        [0, 0, 0],
        [1, 0, 1],
        [0, 0, 0],
        True,
    ]  # undoable actions vs. direct per-port clearing
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
    assert actions["load_action"] == 5.0 and actions["load_action_undo"] == 2.0
    assert actions["template_cleared"] and actions["template_dialog_created"]
    if dialog_stub:
        assert all(actions["dialog_checks"]), actions["dialog_checks"]
        calls = (tmp_path / "dialogs.log").read_text().splitlines()
        assert sum(line.startswith("file ") for line in calls) >= 6
        assert sum(line.startswith("message ") for line in calls) >= 4
    assert actions["paste"] == [True, 5.0] and actions["paste_json"]
    assert actions["selection_reset"] == 5.0 and actions["selection_reset_undo"] == 7.0
    assert 0 <= actions["selection_randomized"] <= 10 and actions["selection_randomized"] != 7.0
    assert actions["selection_randomize_undo"] == 7.0
    assert actions["selection_randomize_redo"] == actions["selection_randomized"]
    assert actions["selection_disconnect"] == [[1, 0, 1], [0, 0, 0], [1, 0, 1]]
    assert actions["selection_clone_count"] == 1 and actions["selection_clone_undo"]
    assert actions["selection_saved"] == ["PyVoice"] and actions["selection_loaded"] == 1
    assert actions["selection_delete_count"] == 0 and actions["selection_delete_undo"] == 1
    assert actions["selection_load_undo"]
    assert actions["selection_pasted"] is None  # Rack's void paste action
    assert actions["selection_paste_count"] == 1 and actions["selection_paste_undo"]
    assert actions["module_paste_count"] == 1 and actions["module_paste_undo"]
    assert actions["reset"] == 5.0 and actions["bypassed"] and actions["cloned"]
    # The clone copies the cable into its input too (Rack's Duplicate with cables): two cables, then none.
    assert actions["disconnected"][2] < actions["reconnected"][2]
    assert actions["plain_disconnect"][0][2] < actions["reconnected"][2]
    assert actions["plain_disconnect"][1]  # no history action, unlike disconnect_action
    assert one["blink"] == [True, ["PyBlink", False], True]  # the documentation's module widget, in Rack

    two = launch(rack_host, tmp_path, plugin, 2)
    assert sorted(two["reloaded"]["rebuilt"]) == sorted(one["ids"])  # Rack's patch loading used the factory
    assert two["reloaded"]["cables"] == one["final_cables"]  # and found the cables' ports on the rebuilt widgets
