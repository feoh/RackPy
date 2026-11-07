# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-52: original permissive-source-inspired examples in actual Rack."""

import json
import shutil
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from tests.examples.support import EXAMPLES, plugin_tool, saved_patch, write_patch
from tests.support import (
    MODULE,
    PLUGINS,
    assert_clean_exit,
    linux_x64_only,
    run_pytest_in_rack,
    run_rack,
)

PROJECTS = tuple(EXAMPLES / name for name in ("envelope", "feedback-governor"))
HERE = Path(__file__).resolve().parent
ENTRY_SETUP = "import rackpyenvelope, rackpyfeedbackgovernor"


def kernels_of(project: Path) -> list[str]:
    return list(json.loads((project / "rackpy-plugin.json").read_text())["kernels"])


def stage_examples(bundle: Path, build: Path) -> Path:
    for project in PROJECTS:
        for item in (project / "pythonpath").iterdir():
            if item.name == "__pycache__":
                continue
            destination = bundle / "pythonpath" / item.name
            if item.is_dir():
                shutil.copytree(item, destination, dirs_exist_ok=True)
            else:
                shutil.copy(item, destination)
        shutil.copytree(project / "res", bundle / "res", dirs_exist_ok=True)
    (bundle / "kernels").mkdir(exist_ok=True)
    libraries = [
        build / "examples" / project.name / "kernels" / f"{name}{MODULE}"
        for project in PROJECTS
        for name in kernels_of(project)
    ]
    for library in libraries:
        shutil.copy(library, bundle / "kernels" / library.name)
    loader = bundle / "rackpy-loader.json"
    data = json.loads(loader.read_text())
    data["kernels"] = [*data.get("kernels", []), *(f"kernels/{lib.name}" for lib in libraries)]
    loader.write_text(json.dumps(data), newline="\n")
    return bundle


def model_entries() -> list[dict[str, Any]]:
    return [entry for project in PROJECTS for entry in json.loads((project / "plugin.json").read_text())["modules"]]


@pytest.mark.host
@linux_x64_only()
def test_both_examples_match_their_reference_inside_rack(
    make_hooks_bundle: Callable[[], Path], hooks_build: Path, rack_host: Path, tmp_path: Path
) -> None:
    result = run_pytest_in_rack(
        stage_examples(make_hooks_bundle(), hooks_build),
        rack_host,
        tmp_path / "user",
        HERE / "in_rack_permissive",
        args=["-s"],
        during_ui=True,
        timeout=600,
        entry_setup=ENTRY_SETUP,
        extra_models=model_entries(),
    )
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert len(outcomes) == 6 and set(outcomes.values()) == {"passed"}, result.summary()


@pytest.mark.host
@linux_x64_only()
@pytest.mark.parametrize("project", PROJECTS, ids=lambda project: project.name)
def test_each_plugin_builds_loads_and_reopens_in_rack(
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
    assert built["kernels"] == [f"kernels/{name}{MODULE}" for name in kernels_of(project)]
    modules = [
        {"id": index + 2, "plugin": slug, "model": model["slug"], "version": manifest["version"], "pos": [0, 0]}
        for index, model in enumerate(manifest["modules"])
    ]
    write_patch(user, modules, [])
    for _ in range(2):
        result = run_rack(rack_host, user, settle=1.5)
        assert_clean_exit(result)
        assert result.lines(f"Loaded plugin {slug} {manifest['version']}"), result.tail()
        for error in ("RackPy skipped kernel library", "RackPy host initialization failed", "Traceback"):
            assert not result.lines(error), (error, result.tail())
        saved = saved_patch(user)
        assert [(module["plugin"], module["model"]) for module in saved["modules"]] == [
            (slug, model["slug"]) for model in manifest["modules"]
        ]
