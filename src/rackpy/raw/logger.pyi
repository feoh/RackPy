# SPDX-License-Identifier: GPL-3.0-or-later
"""rack::logger: Rack's log."""

import enum

class Level(enum.IntEnum):
    DEBUG_LEVEL = 0
    INFO_LEVEL = 1
    WARN_LEVEL = 2
    FATAL_LEVEL = 3

DEBUG_LEVEL: Level
INFO_LEVEL: Level
WARN_LEVEL: Level
FATAL_LEVEL: Level

def log(level: Level, filename: str, line: int, func: str, format: str, *args: int | float | str | bytes) -> None: ...
def get_log_path() -> str: ...
def set_log_path(path: str) -> None: ...
