# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-36: examples/polyphony, examples/dsp and examples/expanders.

Offline: the polyphony teaching loops agree with each other and with the C++ reference
(native/tests/polyphony_reference.cpp), whose scalar and float_4 versions agree; the
Source and Receiver graphs are one frame apart in the offline reference. Natively: the
examples' kernel libraries reference no CPython symbol and make no heap, lock, write or
GIL call over 20 000 frames each (native/tests/example_kernel_guard.cpp under the audio
guard). In Rack (test-hooks build): the oscillators match the C++ reference for 0, 1, 5
and 16 channels and a channel count that changes; the filter matches Rack's own filter
run from Python; the expanders, in Rack's engine on 1, 2 and 4 threads, are one frame
apart, follow rearrangement and removal, and the Peeker reads only a Source. Each
example builds with its kernel library and loads as its own plugin over two launches.
"""

import json
import math
import os
import shutil
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest
import rackpyexpanders_graphs as graphs  # pyproject's pythonpath
import rackpypolyphony_teaching as teaching

from rackpy.dsp.graph_reference import run, run_pair
from tests.examples.support import EXAMPLES, plugin_tool, saved_patch, write_patch
from tests.support import BIN, PLUGINS, assert_clean_exit, linux_x64_only, run_pytest_in_rack, run_rack

POLYPHONY, DSP, EXPANDERS = (EXAMPLES / name for name in ("polyphony", "dsp", "expanders"))
PROJECTS = (POLYPHONY, DSP, EXPANDERS)
HERE = Path(__file__).resolve().parent
ENTRY_SETUP = "import rackpypolyphony, rackpydsp, rackpyexpanders  # add the examples' models"


def kernel_libraries(build: Path) -> list[Path]:
    return [build / "examples" / p.name / "kernels" / f"{name}.so" for p in PROJECTS for name in kernels_of(p)]


def kernels_of(project: Path) -> list[str]:
    names: list[str] = json.loads((project / "rackpy-plugin.json").read_text()).get("kernels", [])
    return names


def polyphony_reference(build: Path) -> dict[str, object]:
    proc = subprocess.run([str(build / "rackpy_polyphony_reference")], capture_output=True, text=True, check=True)
    data: dict[str, object] = json.loads(proc.stdout)
    return data


@pytest.mark.unit
@pytest.mark.parametrize("channels", [0, 1, 5, 16])
def test_the_teaching_loops_agree(channels: int) -> None:
    rate = 48000
    inputs = [[(c - 7.5) / 6 for c in range(channels)] for _ in range(300)]
    one, four = teaching.per_channel(0.25, inputs, rate), teaching.four_at_a_time(0.25, inputs, rate)
    assert [len(f) for f in one] == [max(1, channels)] * 300 and [len(f) for f in four] == [len(f) for f in one]
    assert all(abs(a - b) < 1e-12 for fa, fb in zip(one, four, strict=True) for a, b in zip(fa, fb, strict=True))
    first = (inputs[0][0] if channels else 0.0) + 0.25
    step = teaching.FREQ_C4 * 2**first / rate  # cycles per frame for voice 0
    assert one[0][0] == pytest.approx(5.0 * math.sin(2 * math.pi * step))


@pytest.mark.unit
def test_the_expander_pair_is_one_frame_apart_offline() -> None:
    ramp = [[float(k)] for k in range(20)]
    source, receiver = run_pair(
        graphs.source_graph(),
        graphs.receiver_graph(),
        20,
        sample_rate=48000,
        left_inputs={"inputs": {graphs.SIGNAL: ramp}, "params": {graphs.LEVEL: 0.5}},
    )
    sent = [f[0] for f in source.outputs[graphs.SCALED]]
    got = [f[0] for f in receiver.outputs[graphs.RECEIVED]]
    assert sent == [k * 0.5 for k in range(20)] and got == [0.0, *sent[:-1]]
    assert receiver.lights[graphs.LINK][1:] == [0.5] * 19
    alone = run(graphs.receiver_graph(), 3, sample_rate=48000)  # nothing on its left: nothing arrives
    assert alone.outputs[graphs.RECEIVED] == [[0.0]] * 3 and alone.lights[graphs.LINK] == [0.0] * 3


@pytest.mark.native
@linux_x64_only()
def test_the_cpp_reference_scalar_and_simd_agree(dev_build: Path) -> None:
    reference = polyphony_reference(dev_build)
    rate = reference["rate"]
    assert isinstance(rate, int | float)
    scenarios = reference["scenarios"]
    assert isinstance(scenarios, list) and len(scenarios) == 5
    for s in scenarios:
        pairs = list(zip(s["scalar"], s["simd"], strict=True))
        assert all(len(a) == len(b) == max(1, len(i)) for (a, b), i in zip(pairs, s["inputs"], strict=True))
        simd = max(abs(x - y) for a, b in pairs for x, y in zip(a, b, strict=True))
        python = teaching.per_channel(s["knob"], s["inputs"], rate)
        floats = max(abs(x - y) for a, b in zip(s["scalar"], python, strict=True) for x, y in zip(a, b, strict=True))
        print(f"{s['name']}: float_4 vs scalar C++ {simd:.3g} V; Python teaching vs scalar C++ {floats:.3g} V")
        assert simd <= 2e-5 and floats <= 1e-2, s["name"]


@pytest.mark.native
@linux_x64_only()
def test_the_example_kernels_make_no_audio_path_calls(dev_build: Path) -> None:
    libraries = kernel_libraries(dev_build)
    assert len(libraries) == 3 and all(p.is_file() for p in libraries)
    symbols = subprocess.run(
        [str(BIN / "rackpy-dev"), "audio-symbols", *map(str, libraries)], capture_output=True, text=True, check=False
    )
    assert symbols.returncode == 0, symbols.stdout + symbols.stderr
    guard = subprocess.run(
        [str(dev_build / "rackpy_example_kernel_guard"), *map(str, libraries)],
        capture_output=True,
        text=True,
        check=True,
        env={**os.environ, "LD_PRELOAD": str(dev_build / "audio_guard.so")},
    )
    counts = json.loads(guard.stdout)
    assert sorted(counts) == ["dsp.Biquad", "expanders.Peek", "polyphony.Scalar", "polyphony.Simd"]
    assert all(not any(c.values()) for c in counts.values()), counts


def stage_examples(bundle: Path, build: Path) -> Path:
    """The examples' code, panels and built kernel libraries added to a RackPy bundle."""
    for project in PROJECTS:
        for item in (project / "pythonpath").iterdir():
            if item.name != "__pycache__":
                (shutil.copytree if item.is_dir() else shutil.copy)(item, bundle / "pythonpath" / item.name)
        shutil.copytree(project / "res", bundle / "res", dirs_exist_ok=True)
    (bundle / "kernels").mkdir(exist_ok=True)
    for library in kernel_libraries(build):
        shutil.copy(library, bundle / "kernels" / library.name)
    loader = bundle / "rackpy-loader.json"
    config = json.loads(loader.read_text())
    config["kernels"] = [*config.get("kernels", []), *(f"kernels/{p.name}" for p in kernel_libraries(build))]
    loader.write_text(json.dumps(config))
    return bundle


def manifest_entries() -> list[dict[str, object]]:
    return [entry for project in PROJECTS for entry in json.loads((project / "plugin.json").read_text())["modules"]]


@pytest.mark.host
@pytest.mark.gui
@linux_x64_only()
def test_the_modules_in_live_rack(
    make_hooks_bundle: Callable[[], Path], hooks_build: Path, rack_host: Path, tmp_path: Path
) -> None:
    reference = tmp_path / "polyphony_reference.json"
    reference.write_text(json.dumps(polyphony_reference(hooks_build)))
    result = run_pytest_in_rack(
        stage_examples(make_hooks_bundle(), hooks_build),
        rack_host,
        tmp_path / "user",
        HERE / "in_rack_dsp",
        args=["--polyphony-reference", str(reference), "-s"],
        during_ui=True,
        timeout=600,
        entry_setup=ENTRY_SETUP,
        extra_models=manifest_entries(),
    )
    assert result.exit == 0, result.summary()
    print("".join(line + "\n" for line in result.output.splitlines() if "largest difference" in line), end="")
    outcomes = result.outcomes()
    assert len(outcomes) == 14 and set(outcomes.values()) == {"passed"}, result.summary()


@pytest.mark.host
@linux_x64_only()
@pytest.mark.parametrize("project", PROJECTS, ids=lambda p: p.name)
def test_each_example_loads_as_its_own_plugin(
    make_bundle: Callable[[], Path], dev_build: Path, rack_host: Path, tmp_path: Path, project: Path
) -> None:
    manifest = json.loads((project / "plugin.json").read_text())
    user = tmp_path / "user"
    plugins = user / PLUGINS
    plugins.mkdir(parents=True)
    kernels = dev_build / "examples" / project.name / "kernels"
    plugin_tool("build", project, "--bundle", make_bundle(), "--dest", plugins, "--kernels", kernels)
    slug = manifest["slug"]
    built = json.loads((plugins / slug / "rackpy-loader.json").read_text())
    assert built["kernels"] == [f"kernels/{name}.so" for name in kernels_of(project)]
    modules: list[dict[str, object]] = [
        {"plugin": slug, "model": e["slug"], "version": manifest["version"], "id": 2 + i, "pos": [12 * i, 0]}
        for i, e in enumerate(manifest["modules"])
    ]
    write_patch(user, modules, [])
    for launch in (1, 2):  # the second launch reopens the first's autosave
        run_ = run_rack(rack_host, user, settle=1.5)
        assert_clean_exit(run_)
        assert run_.lines(f"Loaded plugin {slug} {manifest['version']}"), run_.tail()
        for problem in ("RackPy skipped kernel library", "RackPy host initialization failed", "Traceback"):
            assert not run_.lines(problem), (problem, run_.tail())
        saved = saved_patch(user)
        expected = [(slug, e["slug"]) for e in manifest["modules"]]
        assert [(m["plugin"], m["model"]) for m in saved["modules"]] == expected, launch


@pytest.mark.unit
def test_build_refuses_a_project_without_its_kernels(tmp_path: Path) -> None:
    proc = plugin_tool("build", POLYPHONY, "--bundle", tmp_path, "--dest", tmp_path, ok=False)
    assert proc.returncode != 0 and "pass --kernels" in proc.stdout + proc.stderr
    proc = plugin_tool("build", POLYPHONY, "--bundle", tmp_path, "--dest", tmp_path, "--kernels", tmp_path, ok=False)
    assert proc.returncode != 0 and "no kernel library" in proc.stdout + proc.stderr
