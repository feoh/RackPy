# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-33: the plugin-template workflow (bin/rackpy-plugin) and examples/template.

examples/template is exactly what the tool generates from its panel, so the
unit tests regenerate it; the host test builds it on a RackPy bundle and loads
it in actual Rack under its own slug.
"""

import json
import shutil
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.examples.support import EXAMPLES, plugin_tool, project_files, saved_patch, write_patch
from tests.support import PLUGINS, assert_clean_exit, rack_platforms, run_rack

TEMPLATE = EXAMPLES / "template"
CREATE = [
    "create",
    "RackPyTemplate",
    "template",
    "--license",
    "GPL-3.0-or-later",
    "--name",
    "RackPy Template",
    "--brand",
    "RackPy",
    "--author",
    "RackPy contributors",
    "--source-url",
    "https://github.com/feoh/RackPy",
]
ADD = [
    "add-module",
    "Starter",
    "--project",
    "template",
    "--panel",
    "template/res/Starter.svg",
    "--name",
    "Starter",
    "--description",
    "A starting point: one of each component",
    "--tags",
    "Utility",
]


def generate(where: Path) -> Path:
    """examples/template's commands, in ``where``."""
    plugin_tool(*CREATE, cwd=where)
    shutil.copy(TEMPLATE / "res" / "Starter.svg", where / "template" / "res" / "Starter.svg")
    plugin_tool(*ADD, cwd=where)
    return where / "template"


@pytest.mark.unit
def test_the_template_is_what_the_tool_generates(tmp_path: Path) -> None:
    assert project_files(generate(tmp_path)) == project_files(TEMPLATE)


@pytest.mark.unit
def test_add_module_keeps_one_import_line_and_refuses_overwrites(tmp_path: Path) -> None:
    root = generate(tmp_path)
    shutil.copy(root / "res" / "Starter.svg", root / "res" / "Second.svg")
    plugin_tool("add-module", "Second", "--project", root, "--panel", root / "res" / "Second.svg")
    entry = (root / "pythonpath" / "rackpytemplate" / "__init__.py").read_text()
    assert "from . import second, starter\n" in entry
    assert entry.rstrip().endswith('plugin.add_model("Second", second.configure, widget=second.SecondWidget)')
    again = plugin_tool("add-module", "Second", "--project", root, "--panel", root / "res" / "Second.svg", ok=False)
    assert again.returncode != 0 and "--overwrite" in again.stderr
    modules = [m["slug"] for m in json.loads((root / "plugin.json").read_text())["modules"]]
    assert modules == ["Starter", "Second"]  # not added twice


@pytest.mark.unit
@pytest.mark.parametrize(
    ("args", "message"),
    [
        (["create", "bad slug", "--license", "MIT"], "slug may only contain"),
        (["create", "Json", "--license", "MIT", "--entry", "json"], "cannot be the entry package"),
        (["create", "Mine", "--license", "MIT", "--entry", "rackpy"], "cannot be the entry package"),
    ],
)
def test_create_refuses_bad_names(tmp_path: Path, args: list[str], message: str) -> None:
    proc = plugin_tool(*args, cwd=tmp_path, ok=False)
    assert proc.returncode != 0 and message in proc.stderr


@pytest.mark.unit
def test_create_refuses_an_existing_directory_and_needs_a_license(tmp_path: Path) -> None:
    (tmp_path / "Taken").mkdir()
    taken = plugin_tool("create", "Taken", "--license", "MIT", cwd=tmp_path, ok=False)
    assert taken.returncode != 0 and "already exists" in taken.stderr
    unlicensed = plugin_tool("create", "Free", cwd=tmp_path, ok=False)
    assert unlicensed.returncode != 0 and not (tmp_path / "Free").exists()


PANEL = """<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
 width="{width}" height="{height}" viewBox="{view}">{body}</svg>"""


def panel(root: Path, body: str, *, width: str = "20mm", height: str = "100mm", view: str = "0 0 20 100") -> Path:
    path = root / "res" / "Test.svg"
    path.write_text(PANEL.format(width=width, height=height, view=view, body=body))
    return path


@pytest.mark.unit
def test_panel_geometry_units_and_refusals(tmp_path: Path) -> None:
    plugin_tool("create", "Geo", "--license", "MIT", cwd=tmp_path)
    root = tmp_path / "Geo"
    # A view box offset by (10, 20): positions are relative to it, and sizes are not offset (unlike helper.py).
    layer = (
        '<g inkscape:label="components"><rect inkscape:label="screen#Screen" x="12" y="30" width="6" height="4"'
        ' style="fill:#ffff00"/><circle inkscape:label="a" cx="15" cy="70" r="1" fill="#F00"/></g>'
    )
    plugin_tool("add-module", "Geo", "--project", root, "--panel", panel(root, layer, view="10 20 20 100"))
    source = (root / "pythonpath" / "geo" / "geo.py").read_text()
    assert "raw.create_widget(Screen, mm2px(Vec(2.0, 10.0)))" in source
    assert "custom.box.size = mm2px(Vec(6.0, 4.0))" in source
    assert "create_param_centered(cl.RoundBlackKnob, mm2px(Vec(5.0, 50.0)), module, Param.A)" in source
    # Pixel units are SVG pixels at 75 DPI, as helper.py assumes: 75 px is 25.4 mm.
    layer = '<g id="components"><circle cx="75" cy="150" r="2" fill="lime"/></g>'
    test = panel(root, layer, width="150", height="300", view="0 0 150 300")
    plugin_tool("add-module", "Geo", "--project", root, "--panel", test, "--overwrite")
    source = (root / "pythonpath" / "geo" / "geo.py").read_text()
    assert "mm2px(Vec(25.4, 50.8)), module, Input.UNNAMED_0)" in source
    for body, message in (
        ("<g/>", 'no "components" layer'),
        (
            '<g inkscape:label="components"><circle inkscape:label="x" cx="1" cy="1" fill="red"/>'
            '<circle inkscape:label="x" cx="1" cy="9" fill="red"/></g>',
            "both named X",
        ),
    ):
        proc = plugin_tool(
            "add-module", "Geo", "--project", root, "--panel", panel(root, body), "--overwrite", ok=False
        )
        assert proc.returncode != 0 and message in proc.stderr
    outside = tmp_path / "Outside.svg"
    outside.write_text(PANEL.format(width="20mm", height="100mm", view="0 0 20 100", body=layer))
    proc = plugin_tool("add-module", "Geo", "--project", root, "--panel", outside, ok=False)
    assert proc.returncode != 0 and "inside the project" in proc.stderr


@pytest.mark.host
@rack_platforms()
def test_the_template_loads_in_rack_under_its_own_slug(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    user = tmp_path / "user"
    plugins = user / PLUGINS
    plugins.mkdir(parents=True)
    plugin_tool("build", TEMPLATE, "--bundle", make_bundle(), "--dest", plugins)
    assert not (plugins / "RackPyTemplate" / "pythonpath" / "rackpytemplate" / "__pycache__").exists()
    module = {"id": 1, "plugin": "RackPyTemplate", "model": "Starter", "version": "2.0.0", "pos": [0, 0]}
    write_patch(user, [{**module, "params": [{"id": 0, "value": 0.25}]}], [])
    run = run_rack(rack_host, user, settle=1.0)
    assert_clean_exit(run)
    assert run.lines("Loaded plugin RackPyTemplate 2.0.0"), run.tail()
    assert not run.lines("RackPy host initialization failed"), run.tail()
    saved = saved_patch(user)["modules"]
    assert isinstance(saved, list) and [(m["plugin"], m["model"]) for m in saved] == [("RackPyTemplate", "Starter")]
    assert saved[0]["params"][0]["value"] == 0.25
