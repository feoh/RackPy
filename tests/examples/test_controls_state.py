# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-34: examples/configuration, examples/state-and-events and examples/menus.

Offline, the examples' Rack-free modules: the configuration graphs (processing and
custom bypass), the sequencer's graph, and the readers of its saved state, pattern
file and plugin settings, which take patch and settings data as untrusted. In Rack
(test-hooks build), the configuration modules match a C++ reference
(native/tests/controls_reference.hpp) for every tooltip, name, route and frame; the
sequencer's events, JSON and patch storage work in Rack's engine; every menu item
works, a browser preview gets none, and items outliving their module are harmless.
Windowed Rack saves and reopens all of it, plugin settings included, and each
example builds and loads as its own plugin.
"""

import json
import math
import shutil
import struct
from collections.abc import Callable
from pathlib import Path
from typing import TYPE_CHECKING

import pytest
import rackpyconfiguration_dsp as config_dsp  # pyproject's pythonpath
import rackpystateevents_core as core

from rackpy.dsp.graph_reference import run
from tests.examples.support import EXAMPLES, plugin_tool, saved_patch, write_patch
from tests.support import (
    PROBE_MODULE,
    assert_clean_exit,
    linux_x64_only,
    run_pytest_in_rack,
    run_rack,
    run_rack_windowed,
    stage_plugin,
)

if TYPE_CHECKING:  # rackpy.types needs Rack; annotations are not evaluated at run time
    from rackpy.types import JSON

RATE = 48000


def wave(frames: int, channels: int = 1) -> list[list[float]]:
    return [
        [5.0 * math.sin(2 * math.pi * (k / 97 + c * 0.3)) + (c - 1) * 0.5 for c in range(channels)]
        for k in range(frames)
    ]


def flat(frames: list[list[float]]) -> list[float]:
    return [v for frame in frames for v in frame]


@pytest.mark.unit
def test_the_shaper_graph_passes_shapes_and_mutes() -> None:
    graph = config_dsp.shaper_graph()
    signal = wave(200)
    defaults = {config_dsp.LEVEL: 1.0, config_dsp.SCALE: 1.0}
    plain = run(graph, 200, sample_rate=RATE, inputs={0: signal}, params=defaults)
    assert [f[0] for f in plain.outputs[0]] == pytest.approx([f[0] for f in signal], abs=1e-6)
    rectified = run(graph, 200, sample_rate=RATE, inputs={0: signal}, params={**defaults, config_dsp.MODE: 1.0})
    assert [f[0] for f in rectified.outputs[0]] == pytest.approx([abs(f[0]) for f in signal], abs=1e-6)
    half = run(graph, 200, sample_rate=RATE, inputs={0: signal}, params={**defaults, config_dsp.MODE: 2.0})
    assert [f[0] for f in half.outputs[0]] == pytest.approx([max(f[0], 0.0) for f in signal], abs=1e-6)
    muted = run(graph, 200, sample_rate=RATE, inputs={0: signal}, params={**defaults, config_dsp.MUTE: 1.0})
    assert {f[0] for f in muted.outputs[0]} == {0.0}
    offset = run(graph, 3, sample_rate=RATE, params={**defaults, config_dsp.OFFSET: 2.5})
    assert [f for f in offset.outputs[0]] == [[2.5]] * 3  # unpatched: one channel, the offset
    tremolo = run(graph, RATE, sample_rate=RATE, inputs={0: [[1.0]] * RATE}, params={**defaults, config_dsp.DEPTH: 1.0})
    levels = [f[0] for f in tremolo.outputs[0]]
    assert min(levels) == pytest.approx(0.0, abs=1e-6) and max(levels) == pytest.approx(1.0, abs=1e-3)  # 1 Hz, full


@pytest.mark.unit
def test_the_splitter_bypass_keeps_the_inversion() -> None:
    signal = wave(50, 4)
    normal = run(config_dsp.splitter_graph(), 50, sample_rate=RATE, inputs={0: signal}, params={0: 0.5})
    bypassed = run(config_dsp.splitter_bypass_graph(), 50, sample_rate=RATE, inputs={0: signal})
    assert [len(f) for f in normal.outputs[1]] == [4] * 50
    assert flat(normal.outputs[0]) == pytest.approx([v * 0.5 for v in flat(signal)])
    assert flat(normal.outputs[1]) == pytest.approx([-v * 0.5 for v in flat(signal)])
    assert flat(bypassed.outputs[0]) == pytest.approx(flat(signal))
    assert flat(bypassed.outputs[1]) == pytest.approx([-v for v in flat(signal)])


def clocked(frames: int, rises: list[int], width: int = 5) -> list[list[float]]:
    return [[10.0 if any(r <= k < r + width for r in rises) else 0.0] for k in range(frames)]


@pytest.mark.unit
def test_the_sequencer_graph_steps_wraps_and_resets() -> None:
    rises = [10 * (i + 1) for i in range(9)]
    pattern = [i / 15 for i in range(16)]
    result = run(
        core.sequencer_graph(),
        100,
        sample_rate=RATE,
        inputs={core.CLOCK: clocked(100, rises)},
        params={core.LENGTH: 4.0},
    )
    seen = [result.outputs[core.CV][r][0] for r in [0, *rises]]
    assert seen == pytest.approx([pattern[i % 4] for i in range(10)])  # range 0-1 V: the pattern's values
    reset = run(
        core.sequencer_graph(),
        100,
        sample_rate=RATE,
        inputs={core.CLOCK: clocked(100, rises[:3]), core.RESET: clocked(100, [45])},
        params={core.LENGTH: 8.0},
        controls={0: {"range": 10.0, "step0": 0.2}},
    )
    assert [reset.outputs[core.CV][k][0] for k in (5, 10, 20, 30, 44, 45, 99)] == pytest.approx(
        [2.0, 10 / 15, 20 / 15, 30 / 15, 30 / 15, 2.0, 2.0]
    )
    assert reset.lights[core.STEP_LIGHT][10] == 1.0 and reset.lights[core.STEP_LIGHT][9] == 0.0


@pytest.mark.unit
def test_saved_state_takes_only_what_it_understands() -> None:
    state = core.State()
    assert state.to_json() == {"version": 1, "range": 0}
    invalid: list[JSON] = [None, [], "range", {"range": "2"}, {"range": True}, {"range": 7}, {"range": -1}]
    for data in invalid:
        state.load_json(data)
        assert state.range == 0, data
    state.load_json({})  # missing: unchanged
    state.load_json({"version": 99, "range": 3, "added later": {"anything": [1, 2]}})  # newer: known fields read
    assert state.range == 3
    assert core.State().pattern == core.default_pattern()


@pytest.mark.unit
def test_the_pattern_file_round_trips_and_is_checked() -> None:
    state = core.State(pattern=[0.25] * 16)
    data = state.pattern_bytes()
    assert len(data) == 64 and struct.unpack("<16f", data) == (0.25,) * 16
    other = core.State()
    assert other.load_pattern(data) and other.pattern == [0.25] * 16
    assert not other.load_pattern(data[:-1]) and not other.load_pattern(b"") and other.pattern == [0.25] * 16
    odd = struct.pack("<16f", math.nan, -3.0, 7.0, *([0.5] * 13))
    assert other.load_pattern(odd) and other.pattern[:4] == [0.0, 0.0, 1.0, 0.5]


@pytest.mark.unit
def test_plugin_settings_are_checked() -> None:
    settings = core.Settings()
    assert settings.to_json() == {"defaultLength": 8}
    invalid: list[JSON] = [
        None,
        {"defaultLength": 0},
        {"defaultLength": 17},
        {"defaultLength": 4.0},
        {"defaultLength": False},
    ]
    for data in invalid:
        settings.load_json(data)
        assert settings.default_length == 8, data
    settings.load_json({"defaultLength": 12, "other": 1})
    assert settings.default_length == 12


CONFIGURATION, STATE, MENUS = (EXAMPLES / name for name in ("configuration", "state-and-events", "menus"))
PROJECTS = (CONFIGURATION, STATE, MENUS)
HERE = Path(__file__).resolve().parent
ENTRY_SETUP = "import rackpyconfiguration, rackpystateevents, rackpymenus  # add the examples' models"


def manifest_entries() -> list[dict[str, object]]:
    return [entry for project in PROJECTS for entry in json.loads((project / "plugin.json").read_text())["modules"]]


def stage_examples(bundle: Path) -> Path:
    """The examples' code and panels added to a RackPy bundle (which keeps its own manifest)."""
    for project in PROJECTS:
        for item in (project / "pythonpath").iterdir():
            if item.name != "__pycache__":
                (shutil.copytree if item.is_dir() else shutil.copy)(item, bundle / "pythonpath" / item.name)
        shutil.copytree(project / "res", bundle / "res", dirs_exist_ok=True)
        for preset in project.glob("presets/*/*.vcvm"):
            # Rack refuses a preset for another plugin; these modules are the test bundle's, "RackPy".
            staged = bundle / preset.relative_to(project)
            staged.parent.mkdir(parents=True, exist_ok=True)
            staged.write_text(json.dumps({**json.loads(preset.read_text()), "plugin": "RackPy"}))
    return bundle


@pytest.mark.host
@pytest.mark.gui
@linux_x64_only()
def test_the_modules_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(
        stage_examples(make_hooks_bundle()),
        rack_host,
        tmp_path / "user",
        HERE / "in_rack_controls",
        args=["--examples", str(EXAMPLES), "-s"],
        during_ui=True,
        timeout=300,
        entry_setup=ENTRY_SETUP,
        extra_models=manifest_entries(),
    )
    assert result.exit == 0, result.summary()
    print("".join(line + "\n" for line in result.output.splitlines() if "difference from C++" in line), end="")
    outcomes = result.outcomes()
    assert len(outcomes) == 9 and set(outcomes.values()) == {"passed"}, result.summary()


CUSTOM = [0.05 * i for i in range(core.STEPS)]


def session_patch(user: Path) -> None:
    """Launch 1's autosave: sequencers 2-4, a switchboard, a bypassed shaper and splitter; files and settings."""
    sequencer = {"plugin": "RackPy", "model": "Sequencer", "version": "2.0.0"}
    modules: list[dict[str, object]] = [
        {**sequencer, "id": 2, "pos": [8, 0], "params": [{"id": 0, "value": 5}], "data": {"version": 1, "range": 2}},
        {
            **sequencer,
            "id": 3,
            "pos": [14, 0],
            "params": [{"id": 0, "value": 8}],
            "data": {"version": 9, "range": 1, "added": [1]},
        },
        {**sequencer, "id": 4, "pos": [20, 0], "params": [{"id": 0, "value": 8}]},  # no data: a missing field
        {
            "plugin": "RackPy",
            "model": "Switchboard",
            "version": "2.0.0",
            "id": 5,
            "pos": [26, 0],
            "params": [],
            "data": {"octave": 3, "note": 4, "muted": False, "inverted": True},
        },
        {
            "plugin": "RackPy",
            "model": "Shaper",
            "version": "2.0.0",
            "id": 6,
            "pos": [30, 0],
            "bypass": True,
            "params": [{"id": 3, "value": 0.5}],
        },
        {
            "plugin": "RackPy",
            "model": "Splitter",
            "version": "2.0.0",
            "id": 7,
            "pos": [40, 0],
            "bypass": True,
            "params": [],
        },
    ]
    write_patch(user, [PROBE_MODULE, *modules], [])
    storage = user / "autosave" / "modules"
    (storage / "2").mkdir(parents=True)
    (storage / "2" / core.PATTERN_FILE).write_bytes(core.State(pattern=CUSTOM).pattern_bytes())
    (storage / "3").mkdir()
    (storage / "3" / core.PATTERN_FILE).write_bytes(b"\0" * 10)  # the wrong size: ignored
    settings = json.loads((user / "settings.json").read_text())
    settings["pluginSettings"] = {"RackPy": {"defaultLength": 12}}
    (user / "settings.json").write_text(json.dumps(settings))


@pytest.mark.host
@pytest.mark.gui
@linux_x64_only()
def test_rack_saves_and_reopens_state_storage_and_settings(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    user = tmp_path / "user"
    files = {"pythonpath/controls_state_session.py": (HERE / "entries" / "controls_state_session.py").read_text()}
    plugin = stage_plugin(stage_examples(make_hooks_bundle()), user, entry="controls_state_session", files=files)
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"] += manifest_entries()
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    config = plugin / "pythonpath" / "session_config.json"
    session_patch(user)

    first = tmp_path / "first.json"
    config.write_text(json.dumps({"phase": 1, "result": str(first)}))
    run = run_rack_windowed(rack_host, user)
    assert run.returncode == 0 and not run.lines("Fatal signal") and not run.lines("Traceback"), run.tail()
    one = json.loads(first.read_text())
    assert "error" not in one, one["error"]
    loaded = one["loaded"]
    assert loaded["default_length"] == 12  # from settings.json, before any module was made
    assert loaded["2"]["model"] == ["Sequencer", False]  # real modules, not failure placeholders
    assert (loaded["2"]["range"], loaded["2"]["length"]) == (2, 5)
    assert loaded["2"]["pattern"] == pytest.approx(CUSTOM) and loaded["2"]["cv"] == pytest.approx(5 * CUSTOM[0])
    assert (loaded["3"]["range"], loaded["3"]["pattern"]) == (
        1,
        core.default_pattern(),
    )  # newer data read; bad file ignored
    assert (loaded["4"]["range"], loaded["4"]["pattern"]) == (0, core.default_pattern())  # no data, no file: defaults
    assert loaded["switchboard"][:4] == [3, 4, False, True]
    assert loaded["switchboard"][4] == pytest.approx(-(1 + 4 / 12))
    assert loaded["bypassed"] == [True, True] and loaded["shaper_depth"] == 0.5
    randomized = one["randomized"]

    saved = saved_patch(user)
    data = {m["id"]: m.get("data") for m in saved["modules"]}
    assert data[2] == {"version": 1, "range": 2} and data[3] == {"version": 1, "range": 1}
    assert data[4] == {"version": 1, "range": 3} and data[5] == {
        "octave": 0,
        "note": 7,
        "muted": False,
        "inverted": True,
    }
    stored = user / "autosave" / "modules" / "4" / core.PATTERN_FILE  # on_save wrote it as Rack saved
    assert stored.read_bytes() == core.State(pattern=randomized["pattern"]).pattern_bytes()
    assert json.loads((user / "settings.json").read_text())["pluginSettings"]["RackPy"] == {"defaultLength": 4}

    second = tmp_path / "second.json"
    config.write_text(json.dumps({"phase": 2, "result": str(second)}))
    run = run_rack_windowed(rack_host, user)
    assert run.returncode == 0 and not run.lines("Fatal signal") and not run.lines("Traceback"), run.tail()
    two = json.loads(second.read_text())
    assert "error" not in two, two["error"]
    reopened = two["reopened"]
    assert reopened["default_length"] == 4
    assert (reopened["4"]["range"], reopened["4"]["length"]) == (3, randomized["length"])
    assert reopened["4"]["pattern"] == pytest.approx(randomized["pattern"])  # float32 in the file
    assert reopened["2"]["pattern"] == pytest.approx(CUSTOM) and reopened["switchboard"][:4] == [0, 7, False, True]
    assert reopened["bypassed"] == [True, True]


@pytest.mark.host
@linux_x64_only()
@pytest.mark.parametrize("project", PROJECTS, ids=lambda p: p.name)
def test_each_example_loads_as_its_own_plugin(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, project: Path
) -> None:
    manifest = json.loads((project / "plugin.json").read_text())
    user = tmp_path / "user"
    plugins = user / "plugins-lin-x64"
    plugins.mkdir(parents=True)
    plugin_tool("build", project, "--bundle", make_bundle(), "--dest", plugins)
    slug = manifest["slug"]
    modules: list[dict[str, object]] = [
        {
            "plugin": slug,
            "model": entry["slug"],
            "version": manifest["version"],
            "id": 2 + i,
            "pos": [10 * i, 0],
            "params": [],
        }
        for i, entry in enumerate(manifest["modules"])
    ]
    write_patch(user, modules, [])
    for launch in (1, 2):  # the second launch reopens the first's autosave
        run = run_rack(rack_host, user, settle=1.5)
        assert_clean_exit(run)
        assert run.lines(f"Loaded plugin {slug} {manifest['version']}"), run.tail()
        assert not run.lines("RackPy host initialization failed") and not run.lines("Traceback"), run.tail()
        saved = saved_patch(user)
        assert [(m["plugin"], m["model"]) for m in saved["modules"]] == [
            (slug, e["slug"]) for e in manifest["modules"]
        ], launch
    built = sorted(p.relative_to(plugins / slug).as_posix() for p in (plugins / slug).glob("presets/*/*.vcvm"))
    assert built == sorted(p.relative_to(project).as_posix() for p in project.glob("presets/*/*.vcvm"))
