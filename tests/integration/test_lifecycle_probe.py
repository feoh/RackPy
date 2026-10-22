# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-06 actual-host probe: thread affinity, lock state, lifetimes and teardown.

Runs Rack Free 2.6.6 with a window inside Xvfb (and once in screenshot mode)
on Linux x86_64, using the RP-04 loader and the spikes/lifecycle host.

    uv run --with pytest pytest tests/integration/test_lifecycle_probe.py -v

Needs `xvfb-run`. Rack creates and removes /tmp/Rack2 while it loads plugins,
so do not run these in parallel or while another Rack instance is starting.
"""

import json
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
SPIKE = REPO / "spikes" / "lifecycle"
EVENT = re.compile(r"RACKPY_LC event=(?P<name>.+?) role=(?P<role>\w+) tid=(?P<tid>\d+) module=(?P<module>-?\d+)")
pytestmark = [
    pytest.mark.skipif(sys.platform != "linux" or platform.machine() != "x86_64", reason="Linux x86_64 only"),
    pytest.mark.skipif(shutil.which("xvfb-run") is None, reason="needs xvfb-run (xorg-server-xvfb)"),
]


def tool(*args: str) -> str:
    return subprocess.run([str(REPO / "bin" / args[0]), *args[1:]], capture_output=True, text=True, check=True).stdout


@dataclass(frozen=True)
class Host:
    rack: Path
    build: Path
    python: Path


@dataclass(frozen=True)
class Event:
    name: str
    role: str
    tid: str
    module: int
    line: str


@dataclass(frozen=True)
class Run:
    returncode: int | None  # None: killed by the watchdog
    log: str

    def lines(self, marker: str) -> list[str]:
        return [line for line in self.log.splitlines() if marker in line]

    def events(self, name: str | None = None) -> list[Event]:
        found = []
        for line in self.log.splitlines():
            m = EVENT.search(line)
            if m and (name is None or m["name"] == name):
                found.append(Event(m["name"], m["role"], m["tid"], int(m["module"]), line))
        return found


@pytest.fixture(scope="module")
def host(tmp_path_factory: pytest.TempPathFactory) -> Host:
    inputs = json.loads(tool("spike-inputs", "paths"))
    build = tmp_path_factory.mktemp("lifecycle-build")
    subprocess.run(
        [
            "make",
            "-C",
            str(SPIKE),
            f"RACK_DIR={tool('rack-sdk', 'path').strip()}",
            f"PYTHON_HOME={inputs['python-3.14']}",
            f"PYBIND11_DIR={inputs['pybind11']}",
            f"BUILD={build}",
        ],
        check=True,
        capture_output=True,
    )
    return Host(rack=Path(tool("rack-sdk", "host").strip()), build=build, python=Path(inputs["python-3.14"]))


def stage(
    user: Path,
    host: Host,
    *,
    worker: str = "none",
    resets: int = 3,
    churn: int = 5,
    threads: int = 1,
    stop_before_close: bool = True,
    worker_at_load: bool = False,
) -> None:
    plugin = user / "plugins-lin-x64" / "RackPyLifecycle"
    plugin.mkdir(parents=True)
    shutil.copy(host.build / "loader" / "plugin.so", plugin)
    shutil.copy(host.build / "host_lifecycle.so", plugin)
    shutil.copy(SPIKE / "lifecycle_plugin.py", plugin)
    (plugin / "python").symlink_to(host.python)
    manifest = {
        "slug": "RackPyLifecycle",
        "name": "RackPy lifecycle probe",
        "version": "2.0.0",
        "license": "GPL-3.0-or-later",
        "brand": "RackPy",
        "author": "RackPy contributors",
        "modules": [{"slug": "Probe", "name": "Probe", "tags": []}],
    }
    (plugin / "plugin.json").write_text(json.dumps(manifest))
    loader = {
        "host": "host_lifecycle.so",
        "libpython": "lib/libpython3.14.so.1.0",
        "stdlib_marker": "lib/python3.14/os.py",
    }
    (plugin / "spike.json").write_text(json.dumps(loader))
    scenario = {
        "worker": worker,
        "resets": resets,
        "churn": churn,
        "frames_before_close": 5,
        "stop_before_close": stop_before_close,
        "worker_at_load": worker_at_load,
    }
    (plugin / "scenario.json").write_text(json.dumps(scenario))
    # Two adjacent probes so their expanders link; the first carries saved data.
    modules = [
        {
            "id": 21,
            "plugin": "RackPyLifecycle",
            "model": "Probe",
            "version": "2.0.0",
            "data": {"saved_by": "patch"},
            "pos": [0, 0],
        },
        {"id": 22, "plugin": "RackPyLifecycle", "model": "Probe", "version": "2.0.0", "pos": [4, 0]},
    ]
    (user / "autosave").mkdir()
    (user / "autosave" / "patch.json").write_text(json.dumps({"version": "2.6.6", "modules": modules, "cables": []}))
    if threads != 1:
        (user / "settings.json").write_text(json.dumps({"threadCount": threads}))


def run_rack(host: Host, user: Path, *args: str, timeout: float = 90) -> Run:
    """Run windowed Rack in Xvfb. On timeout, kill the whole process group (Rack, xvfb-run and Xvfb)."""
    env = {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}}
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1600x1000x24", str(host.rack / "Rack"), "-u", str(user), *args]
    proc = subprocess.Popen(
        cmd,
        cwd=host.rack,
        env=env,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    try:
        returncode: int | None = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        returncode = None
    log = user / "log.txt"
    return Run(returncode, log.read_text(errors="replace") if log.exists() else "")


def run_headless(host: Host, user: Path, *, settle: float = 3.0, timeout: float = 60) -> Run:
    """Run `Rack -h`, then press Enter after `settle` seconds. Kills the process group on timeout."""
    env = {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}}
    proc = subprocess.Popen(
        [str(host.rack / "Rack"), "-h", "-u", str(user)],
        cwd=host.rack,
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    time.sleep(settle)  # let the patch load and the worker run before pressing Enter
    try:
        proc.communicate(input=b"\n", timeout=timeout)
        returncode: int | None = proc.returncode
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        returncode = None
    log = user / "log.txt"
    return Run(returncode, log.read_text(errors="replace") if log.exists() else "")


def assert_clean(run: Run) -> None:
    assert run.returncode == 0, run.log[-3000:]
    assert not run.lines("Fatal signal"), run.log[-3000:]
    assert run.lines("RACKPY_LC Py_FinalizeEx rc=0")
    assert run.lines("RACKPY_LC gil ") and "engine=0" in run.lines("RACKPY_LC gil ")[0]
    assert not run.lines("(UNEXPECTED)")


def test_windowed_thread_and_lock_map(host: Host, tmp_path: Path) -> None:
    stage(tmp_path, host)
    run = run_rack(host, tmp_path)
    assert_clean(run)

    ui_only = [
        "construct",
        "destruct",
        "onAdd",
        "onRemove",
        "onReset",
        "onRandomize",
        "onBypass",
        "onUnBypass",
        "onSampleRateChange",
        "onSave",
        "dataToJson",
        "dataFromJson",
        "appendContextMenu",
        "widget construct",
        "widget destruct",
        "widget step",
        "widget draw",
        "widget onContextDestroy",
    ]
    for name in ui_only:
        events = run.events(name)
        assert events, f"{name} was never called"
        assert {e.role for e in events} == {"main"}, (name, [e.line for e in events])

    process = run.events("process(first on thread)")
    engine_tids = {e.tid for e in process}
    assert process and {e.role for e in process} == {"engine"}
    expanders = run.events("onExpanderChange")
    assert any(e.tid in engine_tids for e in expanders), "onExpanderChange should also run on the engine thread"
    assert any(e.role == "main" for e in expanders), "and on the main thread when a neighbour is removed"

    # Exclusive lock: the engine stops. Shared lock (save): it keeps processing.
    reset = next(e for e in run.events("onReset") if "processed_during_30ms" in e.line)
    assert "processed_during_30ms=0" in reset.line
    save = next(e for e in run.events("dataToJson") if "processed_during_30ms" in e.line)
    assert int(save.line.split("processed_during_30ms=")[1].split()[0]) > 0

    assert run.lines("reentry from exclusive callback refused")
    assert run.lines('data_from_json role=main text={"saved_by":"patch"}')

    # Teardown order: Python workers stop, the window closes, Rack deletes the
    # context and modules, then destroy() invalidates handles and finalizes.
    order = [
        run.log.index("py: scenario stop"),
        run.log.index("event=scenario close"),
        run.log.index("Deleting context"),
        run.log.index("event=destroy"),
        run.log.index("stale handle after context deletion: ReferenceError"),
        run.log.index("Py_FinalizeEx rc=0"),
    ]
    assert order == sorted(order)
    first_destroy = run.log.index("widget onContextDestroy")
    assert first_destroy < run.log.index("widget destruct")


def test_safe_worker_stress(host: Host, tmp_path: Path) -> None:
    """A Python thread hammers a shared-lock engine call with the GIL released while the UI thread runs
    200 Python callbacks under the exclusive lock and adds and deletes 40 modules."""
    stage(tmp_path, host, worker="safe", resets=200, churn=40)
    run = run_rack(host, tmp_path, timeout=120)
    assert_clean(run)
    assert run.lines("worker role=other has_context=False"), "Python threads have no Rack context of their own"
    (joined,) = run.lines("worker joined")
    assert "alive=False" in joined and "stale=40" in joined
    assert int(joined.split("calls=")[1].split()[0]) > 1000
    assert run.lines("resets=200")


def test_unsafe_worker_deadlocks_and_is_caught_by_the_watchdog(host: Host, tmp_path: Path) -> None:
    """The same worker keeping the GIL while it waits for the engine lock deadlocks against a Python
    callback that Rack runs under the exclusive lock. This is why every engine-locking binding releases
    the GIL first."""
    stage(tmp_path, host, worker="unsafe", resets=200, churn=0)
    run = run_rack(host, tmp_path, timeout=30)
    assert run.returncode is None, "expected a deadlock; Rack exited instead"
    assert run.lines("worker role=other")
    assert run.events("onReset"), "the UI thread entered onReset holding the exclusive lock"
    assert not run.lines("py: on_reset"), "and never obtained the GIL"
    assert not run.lines("scenario stop")


def test_browser_preview_widget_has_null_module(host: Host, tmp_path: Path) -> None:
    stage(tmp_path, host, resets=0, churn=0)
    run = run_rack(host, tmp_path, "-t", "1")
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.log[-3000:]
    null_widgets = [e for e in run.events("widget construct") if "module=null" in e.line]
    assert null_widgets and all(e.role == "main" for e in null_widgets)
    assert any(e.module == -1 for e in run.events("widget draw"))
    assert not run.lines("py: scenario"), "no Python runs for a widget without a module"


def test_multithreaded_engine_keeps_python_off_engine_threads(host: Host, tmp_path: Path) -> None:
    stage(tmp_path, host, resets=3, churn=0, threads=4)
    run = run_rack(host, tmp_path)
    assert_clean(run)
    tids = {e.tid for e in run.events("process(first on thread)")}
    assert len(tids) >= 2, tids


def assert_sentinel_teardown(run: Run, source: str) -> None:
    begin = run.log.index("event=shutdown begin role=main")
    assert source in run.log[begin : begin + 300]
    closing = run.lines("context closing: worker_alive=False")
    assert closing and int(closing[0].split("calls=")[1].split()[0]) > 0
    order = [
        begin,
        run.log.index("shutdown leases drained"),
        run.log.index("Deleting engine"),
        run.log.index("Py_FinalizeEx rc=0"),
    ]
    assert order == sorted(order), "workers must stop and leases drain before Rack deletes the engine"


def test_sentinel_stops_workers_before_the_engine_is_deleted_windowed(host: Host, tmp_path: Path) -> None:
    """No scripted stop: the sentinel's onContextDestroy is the earliest teardown signal."""
    stage(tmp_path, host, worker="safe", resets=50, churn=20, stop_before_close=False)
    run = run_rack(host, tmp_path)
    assert_clean(run)
    assert not run.lines("py: scenario stop")
    assert_sentinel_teardown(run, "sentinel onContextDestroy")


def test_sentinel_stops_workers_before_the_engine_is_deleted_headless(host: Host, tmp_path: Path) -> None:
    """Headless Rack has no window, so the sentinel's destructor during scene deletion is the signal."""
    stage(tmp_path, host, worker="safe", resets=0, churn=0, worker_at_load=True)
    run = run_headless(host, tmp_path)
    assert_clean(run)
    assert_sentinel_teardown(run, "sentinel destructor")
    assert run.log.index("Deleting scene") < run.log.index("event=shutdown begin")
