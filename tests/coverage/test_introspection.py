# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-11: the coverage check's evidence comes from the built package inside Rack, not from source.

Stages the production plugin with an entry module that runs rackpy._introspect
and writes what the package really exports; bin/check_coverage.py then checks
the manifest against that file.
"""

import json
import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import BIN, assert_clean_exit, linux_x64_only, run_rack, stage_plugin

pytestmark = [pytest.mark.host, linux_x64_only()]


@pytest.fixture(scope="module")
def artifact(make_bundle: Callable[[], Path], rack_host: Path, tmp_path_factory: pytest.TempPathFactory) -> Path:
    user = tmp_path_factory.mktemp("introspect")
    out = user / "introspection.json"
    entry = (
        "import json\nfrom rackpy import _introspect\n"
        f"with open({str(out)!r}, 'w') as f:\n    json.dump(_introspect.snapshot(), f, indent=2, sort_keys=True)\n"
    )
    stage_plugin(make_bundle(), user, entry="introspect_entry", files={"pythonpath/introspect_entry.py": entry})
    run = run_rack(rack_host, user, settle=0.3)
    assert_clean_exit(run)
    assert out.exists(), run.tail()
    return out


def test_introspection_comes_from_the_running_package(artifact: Path) -> None:
    data = json.loads(artifact.read_text())
    assert data["rack_version"] == "2.6.6" and data["binding"] == "pybind11 3.1.0"
    objects = data["objects"]
    assert objects["rackpy.raw"]["kind"] == "module"
    assert objects["rackpy.errors.StaleHandleError"]["kind"] == "class"
    assert objects["rackpy.runtime.diagnostics"]["kind"] == "function"
    assert objects["rackpy.raw.math.Vec"]["kind"] == "class"
    assert objects["rackpy.raw.math.Vec.__add__"]["kind"] == "function"
    assert objects["rackpy.raw.Exception.msg"]["kind"] == "attribute"
    assert objects["rackpy.math.Rect"]["kind"] == "class"
    assert objects["rackpy.raw.engine.Module.config_input"]["kind"] == "function"
    assert objects["rackpy.raw.engine.Module.get_light_info"]["kind"] == "function"
    assert objects["rackpy.raw.engine.PortInfoView.module"]["kind"] == "property"
    assert objects["rackpy.raw.engine.LightInfoView"]["kind"] == "class"
    assert objects["rackpy.raw.engine.SwitchLabels"]["kind"] == "class"
    assert objects["rackpy.raw.engine.SwitchLabels.append"]["kind"] == "function"
    assert objects["rackpy.raw.engine.SwitchQuantityView.labels"]["kind"] == "property"
    for method in (
        "get_string",
        "is_min",
        "is_max",
        "set_min",
        "set_max",
        "toggle",
        "move_value",
        "get_range",
        "is_bounded",
        "to_scaled",
        "from_scaled",
        "set_scaled_value",
        "get_scaled_value",
        "move_scaled_value",
    ):
        assert objects[f"rackpy.raw.engine.ParamQuantityView.{method}"]["kind"] == "function"


def test_manifest_checks_against_the_built_package(artifact: Path) -> None:
    proc = subprocess.run(
        [str(BIN / "check_coverage.py"), "check", "--introspection", str(artifact)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr


def test_an_artifact_without_objects_is_refused(tmp_path: Path) -> None:
    empty = tmp_path / "empty.json"
    empty.write_text('{"objects": {}}')
    proc = subprocess.run(
        [str(BIN / "check_coverage.py"), "check", "--introspection", str(empty)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert proc.returncode != 0 and "not an introspection of a built package" in proc.stderr
