# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-37: the quickstart and the oscillator tutorial, done as written, make an oscillator that plays in Rack.

The pages' own `bash` blocks run, in a fresh folder: the repository's `bin/` and
`examples/` stand where a checkout has them, and a temporary Rack user folder stands
for `~/.local/share/Rack2`. The quickstart's first block (clone and build RackPy) is
the test session's own build. The tutorial's three edits are prose; the test makes
them as the page describes. Then:

- with the test-hooks bundle, in windowed Rack, the reader's module outputs a
  ±5 V sine, blinks and shows its knob in hertz;
- with the production bundle, headless, the plugin loads under its slug and keeps
  its patch, and a deliberately broken `configure` gives the placeholder and the
  log lines that the troubleshooting page describes.
"""

import json
import re
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import (
    PLUGINS,
    REPO,
    assert_clean_exit,
    rack_platforms,
    run_rack,
    run_rack_windowed,
)

pytestmark = [pytest.mark.docs, pytest.mark.host, rack_platforms()]
DOCS = REPO / "docs"
BASH = re.compile(r"^```bash\n(.*?)^```", re.MULTILINE | re.DOTALL)
EXAMPLE = REPO / "examples" / "oscillator" / "pythonpath" / "rackpyoscillator" / "oscillator.py"


def blocks(page: str) -> list[str]:
    return BASH.findall((DOCS / page).read_text())


def run_block(block: str, cwd: Path, user: Path, bundle: Path) -> None:
    script = (
        block.replace("bin/", f"{REPO}/bin/")
        .replace("examples/", f"{REPO}/examples/")
        .replace("~/.local/share/Rack2", str(user))
        .replace("/tmp/rackpy-bundle/RackPy", str(bundle))
    )
    proc = subprocess.run(
        ["bash", "-euo", "pipefail", "-c", script], cwd=cwd, capture_output=True, text=True, check=False
    )
    assert proc.returncode == 0, f"{script}\n{proc.stdout}\n{proc.stderr}"


def follow_the_pages(work: Path, user: Path, bundle: Path) -> Path:
    """The quickstart's steps 2 and 3, the tutorial's steps 1 and 3, then the build of step 4; returns the plugin."""
    (user / PLUGINS).mkdir(parents=True)
    quickstart = blocks("quickstart.md")
    assert quickstart[0].startswith("git clone")  # building RackPy: the session's own build stands for it
    create, add_module, build = quickstart[1], quickstart[2], quickstart[3]
    run_block(create, work, user, bundle)
    run_block(add_module, work, user, bundle)
    tutorial = blocks("tutorials/oscillator.md")
    copy_dsp, rebuild = tutorial[0], tutorial[1]
    assert rebuild == build
    run_block(copy_dsp, work, user, bundle)
    module = work / "MyPlugin" / "pythonpath" / "myplugin" / "mymodule.py"
    module.write_text(tutorial_edits(module.read_text()))
    run_block(rebuild, work, user, bundle)
    return user / PLUGINS / "MyPlugin"


def tutorial_edits(source: str) -> str:
    """Step 3's three changes, as the page states them."""
    page = (DOCS / "tutorials" / "oscillator.md").read_text()
    imports = ["from myplugin_dsp import oscillator_graph", "from rackpy.raw.dsp import FREQ_C4"]
    graph = "GRAPH = oscillator_graph(Param.PITCH, Input.PITCH, Output.SINE, Light.BLINK)"
    for text in (*imports, graph):
        assert f"`{text}`" in page, text
    source = source.replace("from enum import IntEnum\n", "from enum import IntEnum\n\n" + "\n".join(imports) + "\n")
    light = "class Light(IntEnum):\n    BLINK = 0\n"
    assert light in source
    source = source.replace(light, light + "\n\n" + graph + "\n")
    start = source.index("def configure(")
    end = source.index("\nclass ", start)
    return source[:start] + example_configure() + "\n" + source[end:]


def example_configure() -> str:
    text = EXAMPLE.read_text()
    start = text.index("def configure(")
    return text[start : text.index("\nclass ", start)]


PROBE = """
import json
from pathlib import Path

import myplugin  # the reader's plugin: adds MyModule, as when Rack imports it
from _rackpy_host import _testing

CONFIG = json.loads((Path(__file__).resolve().parent / "probe_config.json").read_text())
seen = {"sine": [], "light": []}


def step(frame, handle):
    if frame < 5 or "done" in seen:
        return
    module = _testing.engine_handle(2)
    seen["sine"].append(module.outputs[0].get_voltage(0))
    seen["light"].append(module.lights[0].get_brightness())
    lit = {value > 0.5 for value in seen["light"]}
    if len(seen["sine"]) >= 400 or (len(seen["sine"]) >= 120 and lit == {True, False}):
        seen["done"] = True
        seen["knob"] = module.get_param_quantity(0).get_display_value_string()
        seen["names"] = [module.get_input_info(0).get_name(), module.get_output_info(0).get_name()]
        Path(CONFIG["result"]).write_text(json.dumps(seen))
        _testing.close_window()


_testing.set_step_callback(step)
"""


def patch(user: Path, modules: list[dict[str, object]]) -> None:
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    (user / "autosave" / "patch.json").write_text(json.dumps({"version": "2.6.6", "modules": modules, "cables": []}))
    (user / "settings.json").write_text(json.dumps({"showTipsOnLaunch": False}))


MY_MODULE = {"id": 2, "plugin": "MyPlugin", "model": "MyModule", "version": "2.0.0", "params": [{"id": 0, "value": 0}]}


def test_the_tutorial_module_plays_in_rack(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    work, user = tmp_path / "work", tmp_path / "user"
    work.mkdir()
    plugin = follow_the_pages(work, user, make_hooks_bundle())
    configure = (plugin / "pythonpath" / "myplugin" / "mymodule.py").read_text()
    assert example_configure() in configure  # what the example's tests check in Rack is what the reader has
    result = tmp_path / "probe.json"
    (plugin / "pythonpath" / "tutorial_probe.py").write_text(PROBE)
    (plugin / "pythonpath" / "probe_config.json").write_text(json.dumps({"result": str(result)}))
    loader = json.loads((plugin / "rackpy-loader.json").read_text())
    (plugin / "rackpy-loader.json").write_text(json.dumps({**loader, "entry": "tutorial_probe"}))
    manifest = json.loads((plugin / "plugin.json").read_text())
    manifest["modules"].append({"slug": "RuntimeProbe", "name": "Runtime probe", "tags": []})  # the hooks' model
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    probe = {"id": 1, "plugin": "MyPlugin", "model": "RuntimeProbe", "version": "2.0.0", "params": []}
    patch(user, [probe, MY_MODULE])
    run = run_rack_windowed(rack_host, user)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    assert run.lines("Loaded plugin MyPlugin 2.0.0"), run.tail()
    seen = json.loads(result.read_text())
    sine = seen["sine"]
    assert all(-5.0001 <= v <= 5.0001 for v in sine) and max(sine) > 4.0 and min(sine) < -4.0, sine
    assert {v > 0.5 for v in seen["light"]} == {True, False}  # it blinks
    assert seen["knob"] == "261.63", seen["knob"]  # middle C, shown in hertz
    assert seen["names"] == ["1V/octave pitch", "Sine"]


def test_the_tutorial_plugin_on_a_production_bundle_and_a_broken_configure(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    work, user = tmp_path / "work", tmp_path / "user"
    work.mkdir()
    plugin = follow_the_pages(work, user, make_bundle())
    patch(user, [MY_MODULE])
    run = run_rack(rack_host, user, settle=1.0)
    assert_clean_exit(run)
    assert run.lines("RackPy interpreter started") and run.lines("Loaded plugin MyPlugin 2.0.0"), run.tail()
    assert not run.lines("could not create its module"), run.tail()

    # Troubleshooting's "A module shows (failed)": configure raises; Rack gets a placeholder and keeps the module.
    module = plugin / "pythonpath" / "myplugin" / "mymodule.py"
    source = module.read_text()
    marker = '    """Rack calls this to build each module: configure its controls here."""\n'
    assert marker in source
    module.write_text(source.replace(marker, marker + '    raise RuntimeError("deliberately broken")\n'))
    patch(user, [{**MY_MODULE, "params": [{"id": 0, "value": 1.5}]}])
    run = run_rack(rack_host, user, settle=1.0)
    assert_clean_exit(run)
    assert run.lines("RackPy: model MyModule could not create its module"), run.tail()
    assert run.lines("RuntimeError: deliberately broken"), run.tail()
    saved = json.loads((user / "autosave" / "patch.json").read_text())
    assert [(m["model"], m["params"]) for m in saved["modules"]] == [("MyModule", [{"id": 0, "value": 1.5}])]
