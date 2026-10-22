# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-08 distributable build: the skeleton built with the official Rack plugin toolchain.

A host-compiler build needs the build machine's glibc (docs/compatibility.md,
finding 1). This builds with the crosstool-ng toolchain from bin/rack-toolchain,
checks that no library needs a glibc newer than the toolchain's 2.23 floor,
and loads the result into Rack. Selected with `-m dist`, because the toolchain
takes about an hour to build once.

    bin/rack-toolchain build
    uv run pytest -m dist -v
"""

import json
import subprocess
from pathlib import Path

import pytest

from tests.support import BIN, REPO, assert_clean_exit, linux_x64_only, require, run_rack, stage_plugin, tool

pytestmark = [pytest.mark.dist, pytest.mark.host, linux_x64_only()]
GLIBC_FLOOR = "2.23"


@pytest.fixture(scope="module")
def dist_bundle(tmp_path_factory: pytest.TempPathFactory) -> Path:
    found = subprocess.run([str(BIN / "rack-toolchain"), "path"], capture_output=True, text=True, check=False)
    require(
        "rack-toolchain", found.returncode == 0, "build the official Linux toolchain with `bin/rack-toolchain build`"
    )
    build = tmp_path_factory.mktemp("dist-build")
    cmd = [str(BIN / "rackpy-dev"), "build", "--build-dir", str(build), "--toolchain", "rack-lin-x64", "--werror"]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False, cwd=REPO)
    assert proc.returncode == 0, proc.stdout[-4000:] + proc.stderr[-4000:]
    dest = tmp_path_factory.mktemp("dist-bundle")
    return Path(tool("rackpy-dev", "bundle", str(dest), "--build-dir", str(build)).strip().splitlines()[-1])


def test_dist_build_meets_the_glibc_floor(dist_bundle: Path) -> None:
    cmd = [str(BIN / "rackpy-dev"), "inspect", str(dist_bundle), "--max-glibc", GLIBC_FLOOR]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    report = json.loads(proc.stdout[proc.stdout.index("{") :])
    print(json.dumps({name: facts["max_glibc"] for name, facts in report.items()}))


def test_dist_build_loads_into_rack(rack_host: Path, dist_bundle: Path, tmp_path: Path) -> None:
    stage_plugin(dist_bundle, tmp_path)
    run = run_rack(rack_host, tmp_path)
    assert_clean_exit(run)
    assert run.lines("RackPy interpreter started: Python 3.14.9"), run.tail()
    assert run.lines("Py_FinalizeEx returned 0"), run.tail()
