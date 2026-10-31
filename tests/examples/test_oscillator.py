# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-33: examples/oscillator, the tutorial oscillator's feature as an original RackPy plugin.

Offline, the example's pure-Python reference and the C++ reference
(native/tests/oscillator_reference.cpp) both meet the signal checks in
tests/examples/signal.py over fixed input streams at 44.1, 48 and 96 kHz. In Rack
(test-hooks build), the module made from the example's model matches the C++
reference, runs in Rack's engine, previews, survives deletion and undo, carries
its sine over a cable and reopens from Rack's autosave. Built as its own plugin on
a production bundle, it loads under its own slug and keeps its patch.
"""

import json
import shutil
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest
from rackpyoscillator_dsp import offline_reference, oscillator_graph  # pyproject's pythonpath

from rackpy.dsp.graph_reference import run
from tests.examples.signal import check_blink, check_parity, check_sine, stream
from tests.examples.support import EXAMPLES, plugin_tool, saved_patch, write_patch
from tests.support import (
    PROBE_MODULE,
    assert_clean_exit,
    linux_x64_only,
    run_pytest_in_rack,
    run_rack,
    run_rack_windowed,
)

OSCILLATOR = EXAMPLES / "oscillator"
RATES = (44100, 48000, 96000)
KINDS = ("unpatched", "patched")
HERE = Path(__file__).resolve().parent
MANIFEST_ENTRY = json.loads((OSCILLATOR / "plugin.json").read_text())["modules"][0]


def cpp_reference(binary: Path, rate: int, kind: str) -> list[tuple[float, float]]:
    knob, voct = stream(rate, kind)
    lines = [f"{rate} {len(knob)} {0 if voct is None else 1}"]
    lines += [f"{k!r} {(voct[i] if voct is not None else 0.0)!r}" for i, k in enumerate(knob)]
    proc = subprocess.run([str(binary)], input="\n".join(lines) + "\n", capture_output=True, text=True, check=True)
    frames = [tuple(float(v) for v in line.split()) for line in proc.stdout.splitlines()]
    assert len(frames) == len(knob)
    return [(f[0], f[1]) for f in frames]


@pytest.mark.unit
@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("rate", RATES)
def test_the_offline_reference_is_the_oscillator(rate: int, kind: str) -> None:
    knob, voct = stream(rate, kind)
    result = offline_reference(len(knob), sample_rate=rate, knob=knob, voct=voct)
    check_sine([frame[0] for frame in result.outputs[0]], rate, knob, voct)
    check_blink(result.lights[0], rate)


@pytest.mark.unit
def test_the_graph_is_mono_unpatched_and_polyphonic_with_a_cable() -> None:
    assert [len(frame) for frame in offline_reference(2, sample_rate=48000).outputs[0]] == [1, 1]
    poly = run(oscillator_graph(), 2, sample_rate=48000, inputs={0: [[0.0, 1.0, 2.0]] * 2})
    assert [len(frame) for frame in poly.outputs[0]] == [3, 3]  # one sine per input channel
    first = poly.outputs[0][0]
    assert first[1] > first[0] and first[2] > first[1]  # an octave higher moves twice as far in a sample


@pytest.mark.native
@linux_x64_only()
@pytest.mark.parametrize("kind", KINDS)
@pytest.mark.parametrize("rate", RATES)
def test_the_cpp_reference_and_the_graph_agree(dev_build: Path, rate: int, kind: str) -> None:
    knob, voct = stream(rate, kind)
    cpp = cpp_reference(dev_build / "rackpy_oscillator_reference", rate, kind)
    check_sine([f[0] for f in cpp], rate, knob, voct)
    check_blink([f[1] for f in cpp], rate)
    result = offline_reference(len(knob), sample_rate=rate, knob=knob, voct=voct)
    check_parity([frame[0] for frame in result.outputs[0]], result.lights[0], cpp)


def stage_example(bundle: Path) -> Path:
    """The example's code and panel added to a RackPy bundle (which keeps its own manifest)."""
    for item in (OSCILLATOR / "pythonpath").iterdir():
        if item.name != "__pycache__":
            (shutil.copytree if item.is_dir() else shutil.copy)(item, bundle / "pythonpath" / item.name)
    shutil.copytree(OSCILLATOR / "res", bundle / "res", dirs_exist_ok=True)
    return bundle


@pytest.mark.host
@pytest.mark.gui
@linux_x64_only()
def test_the_module_in_live_rack(
    make_hooks_bundle: Callable[[], Path], hooks_build: Path, rack_host: Path, tmp_path: Path
) -> None:
    binary = hooks_build / "rackpy_oscillator_reference"
    reference = {f"{rate}/{kind}": cpp_reference(binary, rate, kind) for rate in RATES for kind in KINDS}
    reference_path = tmp_path / "oscillator_reference.json"
    reference_path.write_text(json.dumps(reference))
    result = run_pytest_in_rack(
        stage_example(make_hooks_bundle()),
        rack_host,
        tmp_path / "user",
        HERE / "in_rack_oscillator",
        args=["--oscillator-reference", str(reference_path), "-s"],
        during_ui=True,
        timeout=300,
        entry_setup="import rackpyoscillator  # adds the example's model",
        extra_models=[MANIFEST_ENTRY],
    )
    assert result.exit == 0, result.summary()
    print("".join(line + "\n" for line in result.output.splitlines() if "C++ reference" in line), end="")
    outcomes = result.outcomes()
    assert len(outcomes) == 11 and set(outcomes.values()) == {"passed"}, result.summary()


def oscillators(plugin: str, knob: float) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    """Two oscillators, the first's sine cabled to the second's V/oct input."""
    module = {"plugin": plugin, "model": "Oscillator", "version": "2.0.0"}
    modules = [
        {**module, "id": 2, "pos": [8, 0], "params": [{"id": 0, "value": knob}]},
        {**module, "id": 3, "pos": [18, 0], "params": [{"id": 0, "value": 0.0}]},
    ]
    cable = {"id": 4, "outputModuleId": 2, "outputId": 0, "inputModuleId": 3, "inputId": 0, "color": "#e8a33d"}
    return modules, [cable]


@pytest.mark.host
@pytest.mark.gui
@linux_x64_only()
def test_rack_creates_deletes_restores_and_reopens_it(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    from tests.support import stage_plugin

    user = tmp_path / "user"
    files = {"pythonpath/oscillator_session.py": (HERE / "entries" / "oscillator_session.py").read_text()}
    plugin = stage_plugin(stage_example(make_hooks_bundle()), user, entry="oscillator_session", files=files)
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append(MANIFEST_ENTRY)
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    config = plugin / "pythonpath" / "session_config.json"
    modules, cables = oscillators("RackPy", 1.0)
    write_patch(user, [PROBE_MODULE, *modules], cables)

    first = tmp_path / "first.json"
    config.write_text(json.dumps({"phase": 1, "result": str(first)}))
    run = run_rack_windowed(rack_host, user)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    one = json.loads(first.read_text())
    assert "error" not in one, one["error"]
    assert one["connected"] == [True, 1] and one["knob"] == 1.0
    assert all(-5.0 <= v <= 5.0 for v in one["sine"] + one["carried"]) and len(set(one["carried"])) > 5
    width, height, _, has_module, same_model = one["preview"]
    assert (round(width, 2), height, has_module, same_model) == (120.0, 380.0, False, True)
    added, existed, model = one["added"]
    assert existed and model == ["Oscillator", False]  # a real module, not the failure placeholder
    assert one["after_delete"] is False and one["after_undo"] is True
    saved = saved_patch(user)
    assert {m["id"] for m in saved["modules"] if m["model"] == "Oscillator"} == {2, 3, added}
    assert [(c["outputModuleId"], c["inputModuleId"]) for c in saved["cables"]] == [(2, 3)]

    second = tmp_path / "second.json"
    config.write_text(json.dumps({"phase": 2, "result": str(second), "added": added}))
    run = run_rack_windowed(rack_host, user)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    two = json.loads(second.read_text())
    assert "error" not in two, two["error"]
    assert two["reopened"] == {"knob": 1.0, "connected": True, "third": True}


@pytest.mark.host
@linux_x64_only()
def test_the_plugin_loads_under_its_own_slug_and_keeps_its_patch(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    user = tmp_path / "user"
    plugins = user / "plugins-lin-x64"
    plugins.mkdir(parents=True)
    plugin_tool("build", OSCILLATOR, "--bundle", make_bundle(), "--dest", plugins)
    modules, cables = oscillators("RackPyOscillator", -1.5)
    write_patch(user, modules, cables)
    for launch in (1, 2):  # the second launch reopens the first's autosave
        run = run_rack(rack_host, user, settle=1.5)
        assert_clean_exit(run)
        assert run.lines("Loaded plugin RackPyOscillator 2.0.0"), run.tail()
        assert not run.lines("RackPy host initialization failed") and not run.lines("Traceback"), run.tail()
        saved = saved_patch(user)
        found = [(m["plugin"], m["model"], m["params"][0]["value"]) for m in saved["modules"]]
        assert found == [("RackPyOscillator", "Oscillator", -1.5), ("RackPyOscillator", "Oscillator", 0.0)], launch
        assert [(c["outputModuleId"], c["inputModuleId"]) for c in saved["cables"]] == [(2, 3)], launch
