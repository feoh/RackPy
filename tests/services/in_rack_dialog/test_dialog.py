# SPDX-License-Identifier: GPL-3.0-or-later
"""The Rack-native chooser selects a path or reports cancellation."""

import os
from pathlib import Path

import pytest
from _rackpy_host import _testing

from rackpy.dialog import choose_directory, open_file, save_file
from rackpy.errors import ContextError


def test_native_file_dialog_actions_and_cancellation(tmp_path: Path) -> None:
    selected = Path(os.environ["RACKPY_DIALOG_FILE"])
    start = tmp_path / "starting folder"

    assert open_file(directory=start, filters="Audio:wav,aiff;Any:*") == selected
    assert save_file(directory=start, filename="take.wav", filters="Audio:wav") == selected
    assert choose_directory(directory=start) == selected

    with pytest.raises(ContextError, match="bound Rack UI context"):
        _testing.with_no_context(open_file)

    os.environ["RACKPY_DIALOG_FILE"] = ""
    assert open_file() is None
    assert save_file() is None
    assert choose_directory() is None


def test_file_dialog_checks_filter_types() -> None:
    with pytest.raises(TypeError):
        open_file(filters=1)  # type: ignore[arg-type]
