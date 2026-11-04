# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the osdialog wrapper in a real Rack host without displaying OS dialogs."""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, rack_platforms(), linux_x64_only()]
HERE = Path(__file__).resolve().parent
DIALOG_STUB = HERE.parent / "app" / "dialog_stub.c"


def test_native_file_dialog_in_rack(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, rack_sdk: Path, tmp_path: Path
) -> None:
    shim = tmp_path / "dialog_stub.so"
    subprocess.run(
        [
            "cc",
            "-shared",
            "-fPIC",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(rack_sdk / "dep/include"),
            str(DIALOG_STUB),
            "-o",
            str(shim),
        ],
        check=True,
    )
    log, details = tmp_path / "dialogs.log", tmp_path / "dialog-details.log"
    selected = tmp_path / "picked sample.wav"
    env = {
        "LD_PRELOAD": str(shim),
        "RACKPY_DIALOG_FILE": str(selected),
        "RACKPY_DIALOG_LOG": str(log),
        "RACKPY_DIALOG_DETAILS": str(details),
    }
    result = run_pytest_in_rack(
        make_hooks_bundle(), rack_host, tmp_path / "user", HERE / "in_rack_dialog", during_ui=True, env=env
    )
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert outcomes and set(outcomes.values()) == {"passed"}, result.summary()
    assert log.read_text().splitlines() == ["file 0", "file 2", "file 1", "file 0", "file 2", "file 1"]
    records = details.read_text().splitlines()
    assert len(records) == 6, records
    assert "filter=Audio:wav:aiff" in records[0] and "filter=Any:*" in records[0], records[0]
    assert "filename=take.wav" in records[1] and "filter=Audio:wav" in records[1], records[1]
    assert "starting folder" in records[0] and "starting folder" in records[1], records[:2]
    assert [record.split(" ", 1)[0] for record in records] == [
        "action=0",
        "action=2",
        "action=1",
        "action=0",
        "action=2",
        "action=1",
    ]
