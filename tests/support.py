# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared helpers for RackPy tests: required-environment checks and a real Rack runner."""

import json
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
BIN = REPO / "bin"
READY = "Loading autosave"


def host_platform() -> str:
    """Rack's name for this computer's platform: lin-x64, mac-arm64, mac-x64 or win-x64 (or "unsupported")."""
    if sys.platform == "darwin":
        return "mac-arm64" if platform.machine() == "arm64" else "mac-x64"
    if sys.platform == "linux" and platform.machine() == "x86_64":
        return "lin-x64"
    if sys.platform == "win32" and platform.machine() == "AMD64":
        return "win-x64"
    return "unsupported"


PLATFORM = host_platform()
MACOS = PLATFORM.startswith("mac-")
# A directory: every test's outcome on this platform, outside Rack and inside it, is appended there as JSON
# lines, the per-platform evidence bin/check_coverage.py evidence reads (RP-42).
EVIDENCE = os.environ.get("RACKPY_TEST_EVIDENCE")


def record_evidence(results: list[dict[str, str]]) -> None:
    """Append test outcomes ({"test", "when", "outcome"}) to this platform's evidence, if it is being kept."""
    if not EVIDENCE or not results:
        return
    folder = Path(EVIDENCE)
    folder.mkdir(parents=True, exist_ok=True)
    with (folder / f"{PLATFORM}-{os.getpid()}.jsonl").open("a", encoding="utf-8") as out:
        for result in results:
            out.write(json.dumps({"platform": PLATFORM, **result}, sort_keys=True) + "\n")


WINDOWS = PLATFORM == "win-x64"
# CMake's names on this platform: object files (MinGW's CMake writes .obj) and MODULE libraries (kernels).
OBJECT = ".cpp.obj" if WINDOWS else ".cpp.o"
MODULE = ".dll" if WINDOWS else ".so"
# AddressSanitizer's options for the native sanitizer runs. Apple's ASan has no LeakSanitizer ("detect_leaks
# is not supported on this platform", RP-40), so leaks are checked on Linux only; the code is the same.
ASAN_OPTIONS = f"detect_leaks={0 if MACOS else 1}:abort_on_error=0"
PLUGINS = f"plugins-{PLATFORM}"  # the plugins folder of a Rack user directory
RACK_HOST = "mac-x64+arm64" if MACOS else PLATFORM  # Rack Free's archive: one universal app on macOS


def rack_program(rack: Path) -> Path:
    """Rack's executable in a Rack Free host directory (on macOS the .app)."""
    if MACOS:
        return rack / "Contents" / "MacOS" / "Rack"
    return rack / ("Rack.exe" if WINDOWS else "Rack")


def kill_tree(proc: subprocess.Popen[bytes]) -> None:
    """Kill a process started with start_new_session and its children (xvfb-run starts Xvfb and Rack)."""
    if WINDOWS:
        proc.kill()  # TerminateProcess: Rack is started directly there, with no helper processes
    else:
        os.killpg(proc.pid, signal.SIGKILL)


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
    """Platform gate for tests of Linux-only tools or mechanisms (ELF inspection, LD_PRELOAD guards,
    the official Linux toolchain, sanitizers, Xvfb)."""
    return pytest.mark.skipif(PLATFORM != "lin-x64", reason="this test uses Linux x86_64-only tools")


def rack_platforms() -> pytest.MarkDecorator:
    """Platform gate: native and host tests run on every Rack SDK platform (Linux x86_64, macOS, Windows x64)."""
    return pytest.mark.skipif(PLATFORM == "unsupported", reason="Rack publishes no SDK for this platform")


def nested_name(qualified: str) -> re.Pattern[str]:
    """The Itanium ABI mangling of a nested name up to its parameters: rack::a::f is 4rack1a1fE.

    Every Rack platform's compiler mangles this way, so symbol lists compare without a demangler, and
    Mach-O's extra leading underscore does not matter. GCC's string ABI may tag the name first
    (getDeviceNameB5cxx11E). Searched without the N or NK (const) before it, so a symbol nested in the
    function (a lambda, a local static) also matches."""
    return re.compile("".join(f"{len(part)}{part}" for part in qualified.split("::")) + r"(?:B\d+\w+?)*E")


def symbol_names(path: Path, *, undefined: bool = False) -> str:
    """The external symbol names a library or object defines (or, with undefined, needs), one per line."""
    if MACOS:  # -g -U -j: external, defined, names only, in Apple's nm and in llvm-nm alike
        command = ["nm", "-uj" if undefined else "-gUj", str(path)]
    elif undefined:
        command = ["nm", "-uj", str(path)]
    else:
        command = ["nm", "-j", "--defined-only", "-D" if path.suffix == ".so" else "-g", str(path)]
    return subprocess.run(command, capture_output=True, text=True, check=True).stdout


def bin_command(name: str) -> list[str]:
    """The command that runs bin/NAME: the script itself, whose shebang is `uv run --script`, or on Windows,
    which runs no shebang scripts, that command spelled out (RP-41)."""
    path = str(BIN / name)
    return ["uv", "run", "--script", path] if sys.platform == "win32" else [path]


def posix_shell() -> str:
    """bash, for commands the docs give for a POSIX shell. On Windows that is Git for Windows' bash: plain
    `bash` there is System32's WSL launcher, which runs a Linux distribution, not this machine's tools."""
    if not WINDOWS:
        return "bash"
    git_bash = Path(os.environ.get("PROGRAMFILES", r"C:\Program Files")) / "Git" / "bin" / "bash.exe"
    require("git-bash", git_bash.is_file(), f"install Git for Windows ({git_bash}); the docs' commands are bash")
    return str(git_bash)


def sanitizer_platforms() -> pytest.MarkDecorator:
    """Platform gate for the dedicated AddressSanitizer and ThreadSanitizer programs: MinGW GCC, Rack's compiler
    on Windows, has neither (RP-41), so CMake builds none there."""
    return pytest.mark.skipif(WINDOWS, reason="MinGW GCC has no AddressSanitizer or ThreadSanitizer")


def tool(*args: str) -> str:
    """Run a bin/ utility and return its stdout; a failure is a setup error with the tool's message."""
    proc = subprocess.run([*bin_command(args[0]), *args[1:]], capture_output=True, text=True, check=False)
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


# ALSA as Rack sees it in tests: no hardware definitions, only a null default PCM.
NULL_ALSA_CONFIG = "pcm.!default {\n    type null\n}\n"


def isolated_audio_env(user: Path) -> dict[str, str]:
    """Keep Rack's audio and MIDI drivers off this machine's sound servers and devices.

    An empty XDG_RUNTIME_DIR hides the PulseAudio, PipeWire and JACK sockets (and the variables below
    point their clients nowhere), and ALSA reads a configuration without hardware definitions. Rack's
    RtAudio drivers (ALSA, PulseAudio, JACK) still load and list no devices, and RtMidi's ALSA driver
    fails to start, so tests never open this machine's audio or MIDI devices.
    """
    runtime = user / "xdg-runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    runtime.chmod(0o700)
    alsa = user / "alsa-null.conf"
    alsa.write_text(NULL_ALSA_CONFIG)
    return {
        "XDG_RUNTIME_DIR": str(runtime),
        "ALSA_CONFIG_PATH": str(alsa),
        "PULSE_SERVER": "unix:" + str(runtime / "no-pulse"),
        "PIPEWIRE_REMOTE": "rackpy-test-none",
        "JACK_NO_START_SERVER": "1",
        "JACK_DEFAULT_SERVER": "rackpy-test-none",
    }


def run_rack(
    rack: Path,
    user: Path,
    *,
    settle: float = 1.0,
    timeout: float = 60,
    env: dict[str, str] | None = None,
    stderr: Path | None = None,
) -> RackRun:
    """Run Rack Free headless with a temporary user directory, then quit it through stdin.

    DISPLAY and WAYLAND_DISPLAY are cleared: Rack shows a blocking dialog, even
    headless, when a patch references an unavailable module. Rack creates and
    removes /tmp/Rack2 while it loads plugins, so runs must not overlap.
    """
    environ = (
        {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}}
        | isolated_audio_env(user)
        | (env or {})
    )
    log = user / "log.txt"
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    patch = user / "autosave" / "patch.json"
    if not patch.exists():
        patch.write_text('{"version": "2.6.6", "modules": [], "cables": []}')
    proc = subprocess.Popen(
        [str(rack_program(rack)), "-h", "-u", str(user)],
        cwd=rack,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
        stderr=stderr.open("w") if stderr is not None else subprocess.DEVNULL,  # e.g. sanitizer reports
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
        try:
            proc.stdin.write(b"\n")
            proc.stdin.close()
        except OSError:  # Rack already quit (Windows: EINVAL, elsewhere EPIPE); its log and status say why
            pass
        returncode = proc.wait(timeout=max(1.0, deadline - time.monotonic()))
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        text = log.read_text(errors="replace") if log.exists() else "(no log)"
        pytest.fail(f"Rack did not exit within {timeout}s:\n{text[-3000:]}")
    return RackRun(returncode, log.read_text(errors="replace") if log.exists() else "")


# CI on GitHub-hosted Macs (no GPU): a test-only Rack plugin lets Rack's window use Apple's software OpenGL
# (tests/fixtures/macos-software-gl), as Linux CI renders with Mesa under Xvfb (RP-42).
MACOS_SOFTWARE_GL = MACOS and os.environ.get("RACKPY_MACOS_SOFTWARE_GL") == "1"
SOFTWARE_GL_SLUG = "RackPyCISoftwareGL"
_software_gl: list[Path] = []


def software_gl_plugin() -> Path:
    """The fixture plugin, built once per session with the Mac's clang."""
    if not _software_gl:
        source = REPO / "tests" / "fixtures" / "macos-software-gl"
        out = Path(tempfile.mkdtemp(prefix="rackpy-software-gl-")) / SOFTWARE_GL_SLUG
        out.mkdir()
        shutil.copy(source / "plugin.json", out / "plugin.json")
        subprocess.run(
            ["clang", "-dynamiclib", "-DGL_SILENCE_DEPRECATION", "-framework", "AppKit", "-framework", "OpenGL",
             str(source / "shim.m"), "-o", str(out / "plugin.dylib")],
            check=True,
        )  # fmt: skip
        _software_gl.append(out)
    return _software_gl[0]


def prepare_window_user(user: Path) -> None:
    """What a windowed Rack run needs in its user folder beyond the test's own plugins (the CI Mac's renderer)."""
    if MACOS_SOFTWARE_GL:
        shutil.copytree(software_gl_plugin(), user / f"plugins-{PLATFORM}" / SOFTWARE_GL_SLUG, dirs_exist_ok=True)


# What Rack logs once it has an OpenGL window, and what GLFW logs when the Mac or Windows offers it none.
GL_READY = "Renderer: "
GL_REFUSED = ("NSGL: Failed to find a suitable pixel format", "WGL: ", "Could not create Window")
_opengl_windows: dict[Path, bool] = {}


def opens_gl_windows(rack: Path, timeout: float = 30) -> bool:
    """Whether this Mac or Windows PC gives Rack an OpenGL window, probed once per Rack: Rack windowed, an empty
    user folder.

    GitHub-hosted macOS runners offer no pixel format Rack's GLFW accepts (GLFW error 65545), and Windows
    runners only Windows' OpenGL 1.1, where Rack needs 2.0; Rack then shows a modal "Could not open GLFW
    window" dialog and waits, so each windowed run would hang until killed.
    """
    if rack not in _opengl_windows:
        with tempfile.TemporaryDirectory(prefix="rackpy-gl-probe-") as tmp:
            user = Path(tmp)
            (user / "settings.json").write_text('{"showTipsOnLaunch": false}')
            prepare_window_user(user)
            proc = subprocess.Popen(
                [str(rack_program(rack)), "-u", str(user)],
                cwd=rack,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            log, found = user / "log.txt", None
            deadline = time.monotonic() + timeout
            while found is None and time.monotonic() < deadline and proc.poll() is None:
                text = log.read_text(errors="replace") if log.exists() else ""
                found = True if GL_READY in text else False if any(r in text for r in GL_REFUSED) else None
                time.sleep(0.2)
            kill_tree(proc)
            proc.wait()
            _opengl_windows[rack] = bool(found)
    return _opengl_windows[rack]


def run_rack_windowed(
    rack: Path,
    user: Path,
    *,
    timeout: float = 120,
    patch: Path | None = None,
    env: dict[str, str] | None = None,
    stderr: Path | None = None,
) -> RackRun:
    """Run Rack with a window on a virtual X display until it exits by itself (the test closes the window).

    Needs xvfb-run on Linux: Rack renders with Mesa's software renderer and nothing appears on the
    desktop. On macOS the window opens in the logged-in session (CI runners have one). On timeout the
    whole process group (Rack, xvfb-run and Xvfb) is killed and returncode is None.
    """
    if MACOS:
        require(
            "macos-opengl",
            opens_gl_windows(rack),
            "a Mac that gives Rack an OpenGL window (a GPU and a logged-in session, or RACKPY_MACOS_SOFTWARE_GL=1 "
            "for Apple's software renderer); GitHub-hosted macOS runners offer no pixel format Rack's GLFW accepts",
        )
    elif WINDOWS:
        require(
            "windows-opengl",
            opens_gl_windows(rack),
            "a Windows PC whose display driver gives Rack OpenGL 2.0 (a GPU, or Mesa's opengl32.dll beside "
            "Rack.exe: bin/rack-sdk mesa --into <Rack folder>); GitHub-hosted Windows runners have only Windows' "
            "OpenGL 1.1",
        )
    else:
        require("xvfb-run", shutil.which("xvfb-run") is not None, "install Xvfb (xorg-server-xvfb or xvfb)")
    environ = (
        {k: v for k, v in os.environ.items() if k not in {"DISPLAY", "WAYLAND_DISPLAY"}}
        | isolated_audio_env(user)
        | (env or {})
    )
    (user / "autosave").mkdir(parents=True, exist_ok=True)
    prepare_window_user(user)
    cmd = [str(rack_program(rack)), "-u", str(user)]
    if not MACOS and not WINDOWS:
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1600x1000x24", *cmd]
    if patch is not None:
        cmd.append(str(patch))  # Rack opens it as File > Open does, instead of the autosave
    proc = subprocess.Popen(
        cmd,
        cwd=rack,
        env=environ,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=stderr.open("w") if stderr is not None else subprocess.DEVNULL,  # e.g. sanitizer reports
        start_new_session=True,
    )
    try:
        returncode: int | None = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_tree(proc)
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


SHARED_FILE_BYTES = 1 << 20


def _copy_or_link(src: str, dst: str) -> object:
    # Large files are the bundle's binaries, which no test writes: share them (a hard link) instead of
    # copying hundreds of megabytes per test into the tmpfs. Small files (JSON, Python) stay copies, so
    # a test may rewrite them.
    if Path(src).stat().st_size >= SHARED_FILE_BYTES:
        try:
            os.link(src, dst)
            return dst
        except OSError:
            pass
    return shutil.copy2(src, dst)


def copy_sharing_binaries(src: Path, dst: Path) -> None:
    """Copy a plugin bundle; files of 1 MiB or more are hard links to the source's (never modify them)."""
    shutil.copytree(src, dst, symlinks=True, copy_function=_copy_or_link)


def stage_plugin(bundle: Path, user: Path, *, entry: str | None = None, files: dict[str, str] | None = None) -> Path:
    """Copy a bundle into a Rack user directory's plugin folder, optionally setting the entry module."""
    plugin = user / PLUGINS / bundle.name
    plugin.parent.mkdir(parents=True, exist_ok=True)
    copy_sharing_binaries(bundle, plugin)
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

    def pytest_runtest_logstart(self, nodeid, location):
        # Which test was running, should Rack hang or die before the result is written.
        with open(RESULT + ".progress", "a", encoding="utf-8") as f:
            f.write(nodeid + "\\n")

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
    entry_setup: str = "",
    extra_models: list[dict[str, object]] | None = None,
    env: dict[str, str] | None = None,
    stderr: Path | None = None,
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
    if extra_models:
        manifest_path = plugin / "plugin.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["modules"].extend(extra_models)
        manifest_path.write_text(json.dumps(manifest))
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
        os.devnull,
        "--confcutdir",
        str(tests),
    ]
    # On Windows, pytest's default fd-level capture dup2s descriptors 0-2 in the embedded CPython's UCRT, and
    # replacing UCRT descriptor 0 closes the console handle Rack's msvcrt stdin also uses: headless Rack then
    # reads end-of-file and quits (RP-41). sys-level capture leaves the descriptors alone.
    if WINDOWS:
        argv.append("--capture=sys")
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
    (plugin / "pythonpath" / "rackpy_in_rack_pytest.py").write_text(entry_setup + "\n" + source)
    run = (
        run_rack_windowed(rack, user, timeout=timeout, env=env, stderr=stderr)
        if during_ui
        else run_rack(rack, user, settle=0.3, timeout=timeout, env=env, stderr=stderr)
    )
    progress = result_path.with_name(result_path.name + ".progress")
    started = progress.read_text(encoding="utf-8").splitlines() if progress.exists() else []
    running = f"the last in-Rack test started: {started[-1]}\n" if started and not result_path.exists() else ""
    assert run.returncode == 0, running + run.tail()
    assert result_path.exists(), "the in-Rack pytest run wrote no result:\n" + running + run.tail()
    data = json.loads(result_path.read_text())
    try:
        suite = tests.resolve().relative_to(REPO).as_posix()
    except ValueError:
        suite = ""  # a suite outside the repository (a test's scratch suite) is no evidence for a record
    if suite:
        record_evidence(
            [
                {"test": f"{suite}/{r['nodeid']}", "when": str(r["when"]), "outcome": str(r["outcome"])}
                | ({"reason": str(r["longrepr"])[:300]} if r["outcome"] == "skipped" and r.get("longrepr") else {})
                for r in data["reports"]
            ]
        )
    return InRackResult(int(data["exit"]), list(data["reports"]), str(data["output"]), run)
