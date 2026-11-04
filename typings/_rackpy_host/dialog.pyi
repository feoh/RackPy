# SPDX-License-Identifier: GPL-3.0-or-later
# Internal native implementation for rackpy.dialog; not a supported user-facing module.
from os import PathLike

def choose_file(
    action: str,
    directory: str | PathLike[str] | None = None,
    filename: str | None = None,
    filters: str | None = None,
) -> str | None: ...
