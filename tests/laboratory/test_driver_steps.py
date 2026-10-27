# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22F: laboratory driver behaviour that needs Rack's UI steps, in windowed Rack (test hooks)."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, linux_x64_only, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent


def test_releases_and_off_main_thread_teardown_at_racks_ui_step(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result_path = tmp_path / "result.json"
    stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp22f_steps",
        files={
            "pythonpath/rp22f_steps.py": (HERE / "entries" / "rp22f_steps.py").read_text(),
            "pythonpath/rp22f_config.json": json.dumps({"result": str(result_path)}),
        },
    )
    write_patch(tmp_path, [PROBE_MODULE])
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]

    # Rack's keyboard route: the delivered port and a dropped driver live until the next UI step.
    assert result["keyboard_inside"] == {"alive": [True, True], "refused": True}
    assert result["keyboard_after_call"] == [True, True]
    assert result["keyboard_after_step"] == [False, False]
    assert result["keyboard_bystander"] == [False, False, -1, -1]  # the dropped driver's teardown ran

    # Rack's own menu item (setDeviceId outside any Python port call): released at the next UI step.
    assert result["menu_after_call"] == [True, 1]
    assert result["menu_after_step"] == [False, [False, False, -1, -1]]

    # Dropped on a Python thread: the Python objects go there, the native objects at the next UI step.
    assert result["off_thread_main"] is False
    assert result["off_python_objects"] == [True, True, True]
    midi_port, audio_port, output = result["off_before_step"]
    assert midi_port == [True, True, -1, 0] and audio_port == [True, True, -1, 0]
    assert output[1] is True  # the output still points at its laboratory device
    assert result["off_native_answers"] == [[0], [0]]
    midi_port, audio_port, output = result["off_after_step"]
    assert midi_port == [False, False, -1, -1] and audio_port == [False, False, -1, -1]
    assert output[1] is False
    assert result["off_after_step_ids"] == [[], []]
