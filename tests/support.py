# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for RackPy tests: required-environment checks and a real Rack runner."""

import json
import os
import platform
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
BIN = REPO / "bin"
READY = "Loading autosave"


def allowed_missing() -> set[str]:
    return {name.strip() for name in os.environ.get("RACKPY_ALLOW_MISSING", "").split(",") if name.strip()}


def require(name: str, available: bool, how: str) -> None:
    """Fail, rather than skip, when a required tool or input is missing.

    A missing SDK, Rack host or tool would otherwise turn a required check into
    a silent pass. Set RACKPY_ALLOW_MISSING=<name>[,<name>...] to skip
    deliberately; the skip is then reported with its reason.
    """
    if available:
        return
    message = f"{name} is required: {how}"
    if name in allowed_missing():
        pytest.skip(f"{message} (skipped because RACKPY_ALLOW_MISSING includes {name})")
    pytest.fail(f"{message}. To skip deliberately, set RACKPY_ALLOW_MISSING={name}.", pytrace=False)


def linux_x64_only() -> pytest.MarkDecorator:
    """Platform gate: native and host tests exist only for Linux x86_64 so far (RP-40, RP-41)."""
    return pytest.mark.skipif(
        sys.platform != "linux" or platform.machine() != "x86_64",
        reason="RackPy native builds and host tests exist only for Linux x86_64 so far",
    )


def tool(*args: str) -> str:
    """Run a bin/ utility and return its stdout; a failure is a setup error with the tool's message."""
    proc = subprocess.run([str(BIN / args[0]), *args[1:]], capture_output=True, text=True, check=False)
    if proc.returncode:
        pytest.fail(f"bin/{' '.join(args)} failed ({proc.returncode}):\n{proc.stderr[-3000:]}", pytrace=False)
    return proc.stdout


def require_tools(*names: str) -> None:
    for name in names:
        require(name, shutil.which(name) is not None, f"install `{name}` and put it on PATH")


@dataclass(frozen=True)
class RackRun:
    returncode: int | None  # None: killed by the watchdog
    log: str

    def lines(self, marker: str) -> list[str]:
        return [line for line in self.log.splitlines() if marker in line]

    def tail(self) -> str:
        return self.log[-4000:]


def run_rack(
    rack: Path, user: Path, *, settle: float = 1.0, timeout: float = 60, env: dict[str, str] | None = None
) -> RackRun:
    """Run Rack Free headless with a temporary user directory, then quit it through stdin.

    DISPLAY and WAYLAND_DISPLAY are cleared: Rack shows a blocking dialog, even
    headless, when a patch references an unavailable module. Rack creates and
    removes /tmp/Rack2 while it loads plugins, so runs must not overlap.
    """
    environ = {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}} | (env or {})
    log = user / "log.txt"
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    patch = user / "autosave" / "patch.json"
    if not patch.exists():
        patch.write_text('{"version": "2.6.6", "modules": [], "cables": []}')
    proc = subprocess.Popen(
        [str(rack / "Rack"), "-h", "-u", str(user)],
        cwd=rack,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=environ,
    )
    deadline = time.monotonic() + timeout
    try:
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
        text = log.read_text(errors="replace") if log.exists() else "(no log)"
        pytest.fail(f"Rack did not exit within {timeout}s:\n{text[-3000:]}")
    return RackRun(returncode, log.read_text(errors="replace") if log.exists() else "")


def run_rack_windowed(rack: Path, user: Path, *, timeout: float = 120) -> RackRun:
    """Run Rack with a window on a virtual X display until it exits by itself (the test closes the window).

    Needs xvfb-run. Rack renders with Mesa's software renderer and nothing
    appears on the desktop. On timeout the whole process group (Rack, xvfb-run
    and Xvfb) is killed and returncode is None.
    """
    require("xvfb-run", shutil.which("xvfb-run") is not None, "install Xvfb (xorg-server-xvfb or xvfb)")
    environ = {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}}
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1600x1000x24", str(rack / "Rack"), "-u", str(user)]
    proc = subprocess.Popen(
        cmd,
        cwd=rack,
        env=environ,
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
    return RackRun(returncode, log.read_text(errors="replace") if log.exists() else "")


def write_patch(user: Path, modules: list[dict[str, object]]) -> None:
    """Write the autosave patch Rack loads at start. Reference only modules that will load:
    Rack shows a blocking dialog for unavailable ones, even headless."""
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    patch = {"version": "2.6.6", "modules": modules, "cables": []}
    (user / "autosave" / "patch.json").write_text(json.dumps(patch))


def assert_clean_exit(run: RackRun) -> None:
    assert run.returncode == 0, run.tail()
    assert not run.lines("Fatal signal"), run.tail()
    assert run.lines("Destroying logger"), run.tail()


def stage_plugin(bundle: Path, user: Path, *, entry: str | None = None, files: dict[str, str] | None = None) -> Path:
    """Copy a bundle into a Rack user directory's plugin folder, optionally setting the entry module."""
    plugin = user / "plugins-lin-x64" / bundle.name
    plugin.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(bundle, plugin, symlinks=True)
    for rel, text in (files or {}).items():
        (plugin / rel).parent.mkdir(parents=True, exist_ok=True)
        (plugin / rel).write_text(text)
    if entry is not None:
        config_path = plugin / "rackpy-loader.json"
        config = json.loads(config_path.read_text())
        config["entry"] = entry
        config_path.write_text(json.dumps(config))
    return plugin


# --- pytest inside Rack ------------------------------------------------------------

# pytest and its pinned dependencies (uv.lock), copied into a staged test bundle only.
PYTEST_PACKAGES = ("pytest", "_pytest", "pluggy", "iniconfig", "packaging", "pygments", "py.py")

IN_RACK_RUNNER = """
import contextlib, io, json, sys
sys.path.insert(0, DEPS)
import pytest


class Collector:
    def __init__(self):
        self.reports = []

    def pytest_runtest_logreport(self, report):
        if report.when == "call" or report.outcome != "passed":
            self.reports.append({"nodeid": report.nodeid, "when": report.when, "outcome": report.outcome,
                                 "longrepr": str(report.longrepr) if report.outcome != "passed" else None})

    def pytest_collectreport(self, report):
        if report.failed:
            self.reports.append({"nodeid": report.nodeid, "when": "collect", "outcome": "failed",
                                 "longrepr": str(report.longrepr)})


collector = Collector()
output = io.StringIO()
with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
    code = pytest.main(ARGS, plugins=[collector])
with open(RESULT, "w") as f:
    json.dump({"exit": int(code), "reports": collector.reports, "output": output.getvalue()}, f)
"""


# Runs the suite from the RuntimeProbe widget's step(), on Rack's UI thread with a live context and engine.
IN_RACK_UI_RUNNER = """
from _rackpy_host import _testing

_started = [False]


def _on_step(frame, handle):
    if _started[0] or frame < 3:
        return
    _started[0] = True
    try:
        _run()
    finally:
        _testing.close_window()


_testing.set_step_callback(_on_step)
"""

PROBE_MODULE = {
    "id": 1,
    "plugin": "RackPy",
    "model": "RuntimeProbe",
    "version": "2.0.0",
    "params": [{"id": 0, "value": 0.5}],
}


@dataclass(frozen=True)
class InRackResult:
    exit: int
    reports: list[dict[str, object]]
    output: str
    run: RackRun

    def outcomes(self) -> dict[str, str]:
        return {str(r["nodeid"]): str(r["outcome"]) for r in self.reports if r["when"] in {"call", "setup", "collect"}}

    def failures(self) -> list[dict[str, object]]:
        return [r for r in self.reports if r["outcome"] == "failed"]

    def summary(self) -> str:
        lines = [f"{r['nodeid']} ({r['when']}): {r['longrepr']}" for r in self.failures()]
        return "\n\n".join(lines) + "\n--- pytest output ---\n" + self.output[-6000:]


def run_pytest_in_rack(
    bundle: Path,
    rack: Path,
    user: Path,
    tests: Path,
    *,
    args: list[str] | None = None,
    timeout: float = 120,
    during_ui: bool = False,
) -> InRackResult:
    """Run a pytest suite inside Rack's embedded interpreter, in the built plugin.

    By default the suite (a directory of test_*.py files that import rackpy)
    runs at plugin start-up, on Rack's main thread before Rack creates its
    window or engine, so it suits code that needs no Rack context. With
    ``during_ui=True`` it runs from the RuntimeProbe widget's step() instead,
    in windowed Rack under Xvfb, with a live context and a running engine;
    that needs the test-hooks bundle. pytest comes from the project
    environment and is copied into this test bundle only, never into a
    production one.
    """
    import importlib.util

    plugin = stage_plugin(bundle, user, entry="rackpy_in_rack_pytest")
    deps = plugin / "pythonpath" / "_rackpy_test_deps"
    deps.mkdir()
    for name in PYTEST_PACKAGES:
        spec = importlib.util.find_spec(name.removesuffix(".py"))
        require("pytest-in-rack", spec is not None and spec.origin is not None, f"the project environment lacks {name}")
        assert spec is not None and spec.origin is not None
        origin = Path(spec.origin)
        if origin.name == "__init__.py":
            shutil.copytree(origin.parent, deps / origin.parent.name, ignore=shutil.ignore_patterns("__pycache__"))
        else:
            shutil.copy(origin, deps / origin.name)
    result_path = user / "in_rack_result.json"
    # --confcutdir keeps the repository's own tests/conftest.py (which ignores */in_rack/*) out of the run.
    argv = [
        str(tests),
        "-p",
        "no:cacheprovider",
        "-q",
        "--rootdir",
        str(tests),
        "-c",
        "/dev/null",
        "--confcutdir",
        str(tests),
    ]
    argv += args or []
    header = f"DEPS = {str(deps)!r}\nRESULT = {str(result_path)!r}\nARGS = {argv!r}\n"
    if during_ui:
        body = "def _run():\n" + "".join(f"    {line}\n" if line else "\n" for line in IN_RACK_RUNNER.splitlines())
        source = (
            header
            + body.replace("    collector = Collector()", "    global collector\n    collector = Collector()")
            + IN_RACK_UI_RUNNER
        )
        write_patch(user, [PROBE_MODULE])
    else:
        source = header + IN_RACK_RUNNER
    (plugin / "pythonpath" / "rackpy_in_rack_pytest.py").write_text(source)
    run = (
        run_rack_windowed(rack, user, timeout=timeout)
        if during_ui
        else run_rack(rack, user, settle=0.3, timeout=timeout)
    )
    assert run.returncode == 0, run.tail()
    assert result_path.exists(), "the in-Rack pytest run wrote no result:\n" + run.tail()
    data = json.loads(result_path.read_text())
    return InRackResult(int(data["exit"]), list(data["reports"]), str(data["output"]), run)
