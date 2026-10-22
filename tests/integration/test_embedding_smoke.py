# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-04 actual-host smoke tests: CPython embedded in a Rack Free 2.6.6 plugin.

Runs real Rack headless (`Rack -h -u <temporary user dir>`) on Linux x86_64.
Pinned inputs are fetched and verified by bin/rack-sdk and bin/spike-inputs.

    uv run --with pytest pytest tests/integration/test_embedding_smoke.py -v

Rack creates and removes /tmp/Rack2 while it loads plugins, so do not run these
in parallel or while another Rack instance is starting.
"""

import json
import os
import platform
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
SPIKE = REPO / "spikes" / "embedding"
READY = "Loading autosave"
pytestmark = pytest.mark.skipif(
    sys.platform != "linux" or platform.machine() != "x86_64",
    reason="the RP-04 spike targets Linux x86_64 only",
)


def tool(*args: str) -> str:
    return subprocess.run([str(REPO / "bin" / args[0]), *args[1:]], capture_output=True, text=True, check=True).stdout


@dataclass(frozen=True)
class Inputs:
    sdk: Path
    rack: Path
    python: dict[str, Path]
    pybind11: Path
    nanobind: Path


@dataclass(frozen=True)
class Run:
    returncode: int
    log: str

    def lines(self, marker: str) -> list[str]:
        return [line for line in self.log.splitlines() if marker in line]


@pytest.fixture(scope="session")
def inputs() -> Inputs:
    spike = json.loads(tool("spike-inputs", "paths"))
    return Inputs(
        sdk=Path(tool("rack-sdk", "path").strip()),
        rack=Path(tool("rack-sdk", "host").strip()),
        python={minor: Path(spike[f"python-{minor}"]) for minor in ("3.14", "3.15")},
        pybind11=Path(spike["pybind11"]),
        nanobind=Path(spike["nanobind"]),
    )


@pytest.fixture(scope="session")
def builds(inputs: Inputs, tmp_path_factory: pytest.TempPathFactory) -> dict[str, Path]:
    out = {}
    for minor, home in inputs.python.items():
        build = tmp_path_factory.mktemp(f"build-{minor}")
        subprocess.run(
            [
                "make",
                "-C",
                str(SPIKE),
                f"RACK_DIR={inputs.sdk}",
                f"PYTHON_HOME={home}",
                f"PYTHON_VERSION={minor}",
                f"PYBIND11_DIR={inputs.pybind11}",
                f"NANOBIND_DIR={inputs.nanobind}",
                f"BUILD={build}",
            ],
            check=True,
            capture_output=True,
        )
        out[minor] = build
    return out


def stage(
    user: Path,
    inputs: Inputs,
    build: Path,
    *,
    slug: str = "RackPySpike",
    host: str = "host_pybind11.so",
    minor: str = "3.14",
    runtime: str | None = "3.14",
    direct: bool = False,
) -> Path:
    plugin = user / "plugins-lin-x64" / slug
    plugin.mkdir(parents=True)
    if direct:
        shutil.copy(build / "direct" / "plugin.so", plugin)
    else:
        shutil.copy(build / "loader" / "plugin.so", plugin)
        shutil.copy(build / host, plugin)
    shutil.copy(SPIKE / "spike_plugin.py", plugin)
    if runtime:
        (plugin / "python").symlink_to(inputs.python[runtime])
    manifest = {
        "slug": slug,
        "name": f"{slug} spike",
        "version": "2.0.0",
        "license": "GPL-3.0-or-later",
        "brand": "RackPy",
        "author": "RackPy contributors",
        "modules": [{"slug": "SpikeModule", "name": "Spike", "tags": []}],
    }
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    config = {"host": host, "libpython": f"lib/libpython{minor}.so.1.0", "stdlib_marker": f"lib/python{minor}/os.py"}
    (plugin / "spike.json").write_text(json.dumps(config))
    return plugin


def write_patch(user: Path, modules: list[tuple[int, str]]) -> None:
    # Only reference modules that will load: Rack shows a blocking dialog for
    # unavailable modules, even when headless.
    entries = [
        {"id": mid, "plugin": slug, "model": "SpikeModule", "version": "2.0.0", "params": [{"id": 0, "value": 1.0}]}
        for mid, slug in modules
    ]
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    (user / "autosave" / "patch.json").write_text(json.dumps({"version": "2.6.6", "modules": entries, "cables": []}))


def run_rack(
    inputs: Inputs, user: Path, *, settle: float = 1.5, timeout: float = 60, env: dict[str, str] | None = None
) -> Run:
    environ = {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}} | (env or {})
    log = user / "log.txt"
    proc = subprocess.Popen(
        [str(inputs.rack / "Rack"), "-h", "-u", str(user)],
        cwd=inputs.rack,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=environ,
    )
    try:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and proc.poll() is None:
            if log.exists() and READY in log.read_text(errors="replace"):
                break
            time.sleep(0.1)
        time.sleep(settle)
        assert proc.stdin is not None
        proc.stdin.write(b"\n")
        proc.stdin.close()
        returncode = proc.wait(timeout=max(1.0, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        pytest.fail(f"Rack did not exit within {timeout}s:\n" + log.read_text(errors="replace")[-3000:])
    return Run(returncode, log.read_text(errors="replace"))


def assert_clean_exit(run: Run) -> None:
    assert run.returncode == 0, run.log[-3000:]
    assert not run.lines("Fatal signal"), run.log[-3000:]
    assert run.lines("Destroying logger") or run.lines("Destroying plugins"), run.log[-3000:]


@pytest.mark.parametrize("minor", ["3.14", "3.15"])
@pytest.mark.parametrize("host", ["host_pybind11.so", "host_nanobind.so"])
def test_loader_host_lifecycle(inputs: Inputs, builds: dict[str, Path], tmp_path: Path, host: str, minor: str) -> None:
    stage(tmp_path, inputs, builds[minor], host=host, minor=minor, runtime=minor)
    write_patch(tmp_path, [(11, "RackPySpike"), (12, "RackPySpike")])
    run = run_rack(inputs, tmp_path)
    assert_clean_exit(run)

    assert run.lines(f"interpreter initialized binding={host.split('_')[1].split('.')[0]}"), run.log[-3000:]
    assert run.lines(f" python={minor}."), "interpreter did not report the bundled version"
    assert run.lines("flags isolated=1 no_site=1 dont_write_bytecode=1")
    prefix = [line for line in run.lines("py: python ") if "prefix=" in line]
    assert prefix and "/plugins-lin-x64/RackPySpike/python" in prefix[0]
    assert not [line for line in run.lines("py: import ") if "FAILED" in line], run.lines("py: import ")
    assert run.lines("py: value Vec(4, 6) x=4.0")
    assert len(run.lines("py: configure id=")) == 2
    assert all("thread=main" in line for line in run.lines("widget callback"))
    deleted = run.lines("module deleted")
    assert len(deleted) == 2 and all("thread=main" in line for line in deleted)
    assert all(int(line.split("processed=")[1].split()[0]) > 1000 for line in deleted), deleted
    assert len(run.lines("ReferenceError (module")) == 2
    assert run.lines("Py_FinalizeEx rc=0")
    assert not run.lines("(UNEXPECTED)")
    assert not run.lines("RACKPY_SPIKE_LOADER refused")
    assert run.lines("host init context=null")
    assert all("context=set" in line for line in run.lines("widget callback"))
    assert run.lines("attach outside configure: ")

    # Rack frees widget-tree children with `delete`. A Python subclass handed to
    # it needs C++-heap storage that outlives the Python reference.
    if "pybind11" in host:
        assert len(run.lines("child storage inside python object=False")) == 2
        assert len(run.lines("py: attach: ok")) == 2
        assert len(run.lines("child on_add")) == 2
        scene = run.log.index("Deleting scene")
        after = run.log[scene:]
        assert after.count("child on_remove") == 2 and after.count("child finalized") == 2, after[:2000]
        assert run.lines("attach outside configure: RuntimeError")
    else:
        assert len(run.lines("child storage inside python object=True")) == 2
        assert len(run.lines("py: attach: FAILED TypeError")) == 2
        assert not run.lines("child on_add")


def test_direct_link_breaks_extension_modules(inputs: Inputs, builds: dict[str, Path], tmp_path: Path) -> None:
    """Linking libpython into plugin.so makes it RTLD_LOCAL; modules that expect global Py* symbols fail."""
    stage(tmp_path, inputs, builds["3.14"], direct=True)
    write_patch(tmp_path, [(11, "RackPySpike")])
    run = run_rack(inputs, tmp_path)
    assert_clean_exit(run)
    failed = [line for line in run.lines("py: import ") if "FAILED" in line]
    assert len(failed) == 1 and "import _dbm" in failed[0] and "undefined symbol" in failed[0], failed
    assert run.lines("py: configure id=11")


def test_missing_runtime_is_actionable(inputs: Inputs, builds: dict[str, Path], tmp_path: Path) -> None:
    stage(tmp_path, inputs, builds["3.14"], runtime=None)
    write_patch(tmp_path, [])
    run = run_rack(inputs, tmp_path, settle=0.5)
    assert_clean_exit(run)
    assert run.lines("bundled Python runtime missing or incomplete"), run.log[-3000:]
    assert not run.lines("interpreter initialized")


def test_wrong_runtime_version_is_refused(inputs: Inputs, builds: dict[str, Path], tmp_path: Path) -> None:
    stage(tmp_path, inputs, builds["3.14"], minor="3.14", runtime="3.15")
    write_patch(tmp_path, [])
    run = run_rack(inputs, tmp_path, settle=0.5)
    assert_clean_exit(run)
    assert run.lines("bundled Python runtime missing or incomplete")
    assert not run.lines("interpreter initialized")


def test_second_rackpy_plugin_is_refused(inputs: Inputs, builds: dict[str, Path], tmp_path: Path) -> None:
    """One interpreter owner per process: a second Python-embedding plugin must not initialize."""
    stage(tmp_path, inputs, builds["3.14"], slug="RackPySpike")
    stage(tmp_path, inputs, builds["3.14"], slug="RackPySpikeB", host="host_nanobind.so")
    write_patch(tmp_path, [])
    run = run_rack(inputs, tmp_path, settle=0.5)
    assert_clean_exit(run)
    assert len(run.lines("interpreter initialized")) == 1
    assert len(run.lines("a CPython runtime is already loaded in this process")) == 1


SYSTEM_LIBPYTHON = Path("/usr/lib/libpython3.14.so.1.0")


@pytest.mark.skipif(not SYSTEM_LIBPYTHON.exists(), reason="needs a system libpython to preload")
def test_foreign_preloaded_python_is_refused(inputs: Inputs, builds: dict[str, Path], tmp_path: Path) -> None:
    stage(tmp_path, inputs, builds["3.14"])
    write_patch(tmp_path, [])
    run = run_rack(inputs, tmp_path, settle=0.5, env={"LD_PRELOAD": str(SYSTEM_LIBPYTHON)})
    assert_clean_exit(run)
    refused = run.lines("a CPython runtime is already loaded in this process")
    assert refused and str(SYSTEM_LIBPYTHON) in refused[0]
    assert not run.lines("interpreter initialized")


def test_failing_script_reports_without_crashing(inputs: Inputs, builds: dict[str, Path], tmp_path: Path) -> None:
    plugin = stage(tmp_path, inputs, builds["3.14"])
    (plugin / "spike_plugin.py").write_text("raise RuntimeError('deliberate spike failure')\n")
    write_patch(tmp_path, [])
    run = run_rack(inputs, tmp_path, settle=0.5)
    assert_clean_exit(run)
    failed = run.lines("host init failed (3)")
    assert failed and "deliberate spike failure" in failed[0], run.log[-3000:]
