# SPDX-License-Identifier: GPL-3.0-or-later
"""The facade's names and signatures match the reviewed snapshot (docs/reference/api-stability.md)."""

import json
import subprocess
from pathlib import Path

import pytest

from tests.support import BIN, tool

pytestmark = pytest.mark.unit
SNAPSHOT = Path(__file__).resolve().parent / "surface.json"


def test_the_facade_matches_its_snapshot() -> None:
    tool("facade-surface", "check")


def test_a_changed_signature_or_name_is_reported(tmp_path: Path) -> None:
    recorded = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
    recorded["rackpy.midi"]["note_on"] = "def note_on(note: int, velocity: int=100, channel: int=0) -> Message"
    del recorded["rackpy.assets"]["user"]
    recorded["rackpy.assets"]["removed_helper"] = "def removed_helper() -> None"
    altered = tmp_path / "surface.json"
    altered.write_text(json.dumps(recorded), encoding="utf-8")
    proc = subprocess.run(
        [str(BIN / "facade-surface"), "check", "--against", str(altered)], capture_output=True, text=True, check=False
    )
    assert proc.returncode == 1
    assert "rackpy.midi.note_on: changed" in proc.stderr
    assert "rackpy.assets.user: added" in proc.stderr
    assert "rackpy.assets.removed_helper: removed" in proc.stderr
