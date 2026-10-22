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


def run_rack(rack: Path, user: Path, *, settle: float = 1.0, timeout: float = 60) -> RackRun:
    """Run Rack Free headless with a temporary user directory, then quit it through stdin.

    DISPLAY and WAYLAND_DISPLAY are cleared: Rack shows a blocking dialog, even
    headless, when a patch references an unavailable module. Rack creates and
    removes /tmp/Rack2 while it loads plugins, so runs must not overlap.
    """
    environ = {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}}
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
