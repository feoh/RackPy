# SPDX-License-Identifier: GPL-3.0-or-later
"""Rack's native modal file chooser, for plugin-owned files and resources.

These dialogs wait for the user on Rack's main/UI thread. Call them from a UI
callback, never from module processing or a Rack engine callback. Choosing a
path does not open, execute, copy, or deserialize the selected file.
"""

from __future__ import annotations

import os
from pathlib import Path

from _rackpy_host import dialog as _dialog

__all__ = ["choose_directory", "open_file", "save_file"]


def _choose(
    action: str, directory: str | os.PathLike[str] | None, filename: str | None, filters: str | None
) -> Path | None:
    selected = _dialog.choose_file(action, directory, filename, filters)
    return None if selected is None else Path(selected)


def open_file(*, directory: str | os.PathLike[str] | None = None, filters: str | None = None) -> Path | None:
    """Show Rack's open-file dialog; return the selected path, or ``None`` on cancel.

    ``filters`` uses osdialog syntax, for example ``"Audio:wav,aiff;Any:*"``.
    The starting directory is optional; the operating system chooses one if omitted.
    """
    return _choose("open", directory, None, filters)


def save_file(
    *, directory: str | os.PathLike[str] | None = None, filename: str | None = None, filters: str | None = None
) -> Path | None:
    """Show Rack's save-file dialog; return the chosen destination, or ``None`` on cancel.

    This only selects a destination; it does not create or overwrite the file.
    """
    return _choose("save", directory, filename, filters)


def choose_directory(*, directory: str | os.PathLike[str] | None = None) -> Path | None:
    """Show Rack's directory picker; return the chosen directory, or ``None`` on cancel."""
    return _choose("open_dir", directory, None, None)
