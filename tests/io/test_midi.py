# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29A: rack::midi from Python, in actual Rack, through Rack's Loopback MIDI driver."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, linux_x64_only, run_pytest_in_rack, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent


def test_midi_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, HERE / "in_rack_midi", during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()


def test_python_inputs_receive_queued_messages_on_the_ui_thread(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result_path = tmp_path / "result.json"
    stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp29a_midi",
        files={
            "pythonpath/rp29a_midi.py": (HERE / "entries" / "rp29a_midi.py").read_text(),
            "pythonpath/rp29a_config.json": json.dumps({"result": str(result_path)}),
        },
    )
    write_patch(tmp_path, [PROBE_MODULE])
    (tmp_path / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]
    assert result["startup"] == "refused"
    assert result["before_step"] == []  # never on the sending thread
    assert result["after_step"] == [0, 1, 2, 3, 4]
    assert result["overflow"] == [1024, 976]  # the bounded queue drops the rest, and counts them
    assert result["threads"] == [True]  # the UI (main) thread only
