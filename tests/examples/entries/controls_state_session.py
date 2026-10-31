# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-34 test entry module: the RP-34 examples in windowed Rack, through two launches.

Importing the examples adds their models, as their own entry packages do; the state
example also hands Rack its plugin settings. Launch 1 opens a patch the test wrote
(sequencers with valid, newer and missing data and with good, wrong-size and absent
pattern files; a switchboard; a bypassed shaper and splitter), records what the
modules loaded, then changes some of it; Rack saves the patch and its settings as it
exits. Launch 2 reopens them and records what came back.
"""

import json
import traceback
from pathlib import Path
from typing import Any

import rackpyconfiguration  # noqa: F401  # adds the examples' models, as their entry packages do
import rackpymenus  # noqa: F401
import rackpystateevents  # noqa: F401
import rackpystateevents_core as core
from _rackpy_host import _testing
from rackpymenus.switchboard import Switchboard
from rackpystateevents.sequencer import SETTINGS, Sequencer

from rackpy.raw.engine import Module

CONFIG: dict[str, Any] = json.loads((Path(__file__).resolve().parent / "session_config.json").read_text())
RESULT: dict[str, Any] = {}
SEQUENCERS = (2, 3, 4)
SWITCHBOARD, SHAPER, SPLITTER = 5, 6, 7


def handle(module_id: int) -> Module:
    module = _testing.engine_handle(module_id)
    assert module is not None, f"no module {module_id}"
    return module


def sequencer(module_id: int) -> Sequencer:
    found = Sequencer.of(handle(module_id))
    assert found is not None, f"module {module_id} has no Sequencer"
    return found


def record() -> dict[str, Any]:
    """What each module has, as the patch and its files gave it to Python and the engine."""
    seen: dict[str, Any] = {"default_length": SETTINGS.default_length}
    for module_id in SEQUENCERS:
        s = sequencer(module_id)
        m = s.module
        seen[str(module_id)] = {
            "range": s.state.range,
            "pattern": s.state.pattern,
            "length": m.params[core.LENGTH].value,
            "cv": m.outputs[core.CV].get_voltage(),
            "model": _testing.module_model(module_id),
        }
    board = Switchboard.of(handle(SWITCHBOARD))
    assert board is not None
    seen["switchboard"] = [
        board.octave,
        board.note,
        board.muted,
        board.inverted,
        handle(SWITCHBOARD).outputs[0].get_voltage(),
    ]
    seen["bypassed"] = [handle(SHAPER).is_bypassed(), handle(SPLITTER).is_bypassed()]
    seen["shaper_depth"] = handle(SHAPER).params[3].value
    return seen


def first() -> None:
    RESULT["loaded"] = record()
    # Changes for launch 2 to find: a range, a randomized pattern, the plugin's default length, a pitch.
    s = sequencer(4)
    s.set_range(3)
    _testing.engine_randomize(s.module)
    RESULT["randomized"] = {"pattern": s.state.pattern, "length": s.module.params[core.LENGTH].value}
    SETTINGS.default_length = 4
    board = Switchboard.of(handle(SWITCHBOARD))
    assert board is not None
    board.choose(0, 7)


def second() -> None:
    RESULT["reopened"] = record()


done = [False]


def finish() -> None:
    done[0] = True
    Path(CONFIG["result"]).write_text(json.dumps(RESULT))
    _testing.close_window()


def on_step(frame: int, _: object) -> None:
    if done[0] or frame < 10:  # let Rack's engine run the kernels for a few UI frames first
        return
    try:
        (first if CONFIG["phase"] == 1 else second)()
    except BaseException:
        RESULT["error"] = traceback.format_exc()
    finish()


_testing.set_step_callback(on_step)
