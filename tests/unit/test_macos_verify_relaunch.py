# SPDX-License-Identifier: GPL-3.0-or-later
"""packaging/macos/verify.sh (RP-57): which run of Rack is repeated, and how often.

The script's own launch functions run here against a stand-in for Rack, under the bash that runs the script
(on a Mac: /bin/bash 3.2). Rack dying in CoreMIDI cannot be provoked, so this is the control flow only.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from tests.support import REPO

pytestmark = [
    pytest.mark.unit,
    pytest.mark.skipif(sys.platform == "win32", reason="a bash script for macOS; needs a POSIX shell and mkfifo"),
]

SCRIPT = REPO / "packaging" / "macos" / "verify.sh"

# Stands in for Rack: writes the start of Rack's log, then does what STUB_MODE says. STUB_COUNT counts launches.
STUB = """\
#!/bin/sh
ulimit -c 0
user=; headless=no; shot=no
while [ $# -gt 0 ]; do
    case "$1" in -u) user=$2; shift ;; -h) headless=yes ;; -t) shot=yes; shift ;; esac
    shift
done
n=$(( $(cat "$STUB_COUNT" 2>/dev/null || echo 0) + 1 )); echo "$n" > "$STUB_COUNT"
log=$user/log.txt
printf '[0.01 info main] Initializing audio\\n[0.09 info main] Initializing MIDI\\n' > "$log"
plugins() { echo "[0.17 info main] Initializing plugins" >> "$log"; }
midi() { echo; echo "MidiInCore::initialize: error creating OS-X MIDI client object (-50)."; }
ok() {
    plugins; echo "[1.0 info main] Loading autosave" >> "$log"
    if [ "$headless" = yes ]; then echo "Press enter to exit."; read -r _; exit 0; fi
    if [ "$shot" = yes ]; then exit 0; fi
    echo "[1.1 info window] Renderer: stub" >> "$log"; exec sleep 30
}
case "$STUB_MODE" in
    ok) ok ;;
    midi-once) if [ "$n" = 1 ]; then midi; kill -ABRT $$; fi; ok ;;
    midi-always) midi; kill -ABRT $$ ;;
    other-abort) echo "Assertion failed: something else"; kill -ABRT $$ ;;
    after-plugins) plugins; midi; kill -ABRT $$ ;;
    midi-exit-1) midi; exit 1 ;;
esac
"""

# $1: the stand-in, $2: logs folder, $3: user folder, $4: headless | windowed | gl_probe. The watchdog here is
# a plain sleep, so that nothing outlives the test.
HARNESS = """\
set -uo pipefail
RACK=$1; LOGS=$2; user=$3
SUMMARY=$LOGS/summary.txt; : > "$SUMMARY"; failed=0; GL=unset
watchdog() { sleep 600 > /dev/null 2>&1 & echo $!; }
{functions}
case "$4" in
    headless) headless "$user" case > /dev/null ;;
    *) relaunching "$4" "$user" case > /dev/null ;;
esac
echo "$failed $midi_aborts $GL" > "$LOGS/result"
"""


def functions() -> str:
    """The launch functions of the script, as written there."""
    lines = SCRIPT.read_text().splitlines()

    def block(first: str, last: str) -> list[str]:
        start = next(i for i, line in enumerate(lines) if re.match(first, line))
        end = next(i for i in range(start, len(lines)) if re.match(last, lines[i]))
        return lines[start : end + 1]

    return "\n".join(
        block(r"note\(\) \{", r"note\(\) \{")
        + block(r"images\(\) \{", r"\}")
        + block(r"MIDI_ABORT=", r"headless\(\) \{")
        + block(r"headless_once\(\) \{", r"\}")
        + block(r"gl_probe\(\) \{", r"\}")
        + block(r"windowed\(\) \{", r"\}")
    )


def run(tmp_path: Path, kind: str, mode: str) -> tuple[int, str, str, str, str, Path]:
    """(launches, Rack's last exit status, failed, repeats counted, summary, logs folder)."""
    stub = tmp_path / "Rack"
    stub.write_text(STUB)
    stub.chmod(0o755)
    harness = tmp_path / "harness.sh"
    harness.write_text(HARNESS.replace("{functions}", functions()))
    logs, user = tmp_path / "logs", tmp_path / "user"
    logs.mkdir()
    user.mkdir()
    bash = "/bin/bash" if sys.platform == "darwin" else shutil.which("bash")
    assert bash, "bash is needed for packaging/macos/verify.sh"
    proc = subprocess.run(
        [bash, str(harness), str(stub), str(logs), str(user), kind],
        env={"PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "STUB_MODE": mode, "STUB_COUNT": str(logs / "launches")},
        capture_output=True,
        text=True,
        check=False,
        timeout=120,
    )
    assert proc.returncode == 0, proc.stderr
    failed, repeats, _gl = (logs / "result").read_text().split()
    launches = int((logs / "launches").read_text())
    return launches, (logs / "case.rc").read_text().strip(), failed, repeats, (logs / "summary.txt").read_text(), logs


@pytest.mark.parametrize("kind", ["headless", "windowed", "gl_probe"])
def test_rack_dying_in_its_midi_start_up_is_run_once_more_and_counted(tmp_path: Path, kind: str) -> None:
    launches, status, failed, repeats, summary, logs = run(tmp_path, kind, "midi-once")
    assert (launches, failed, repeats) == (2, "0", "1")
    assert status == ("137" if kind == "gl_probe" else "0")  # the probe kills Rack once it has its answer
    assert summary.startswith("AGAIN case: Rack died in its own MIDI start-up (exit status 134)")
    assert "FAIL" not in summary
    assert (logs / "case.midi-abort-1.rc").read_text().strip() == "134"  # the first run is kept
    assert "MIDI client object" in (logs / "case.midi-abort-1.out").read_text()
    assert "Initializing plugins" not in (logs / "case.midi-abort-1.log").read_text()


def test_a_second_death_in_a_row_fails_and_is_not_run_a_third_time(tmp_path: Path) -> None:
    launches, status, failed, repeats, summary, _ = run(tmp_path, "windowed", "midi-always")
    assert (launches, status, failed, repeats) == (2, "134", "1", "1")
    assert "FAIL  case: Rack died in its own MIDI start-up twice" in summary


@pytest.mark.parametrize(
    ("mode", "status"),
    [
        ("ok", "0"),
        ("other-abort", "134"),  # killed by a signal, but not with the MIDI message
        ("after-plugins", "134"),  # the MIDI message, but plugins had begun to load
        ("midi-exit-1", "1"),  # the MIDI message, but Rack was not killed by a signal
    ],
)
def test_nothing_else_is_repeated(tmp_path: Path, mode: str, status: str) -> None:
    launches, last, failed, repeats, summary, logs = run(tmp_path, "windowed", mode)
    assert (launches, last, failed, repeats, summary) == (1, status, "0", "0", "")
    assert not list(logs.glob("*midi-abort*"))
