# SPDX-License-Identifier: GPL-3.0-or-later
"""Paths to this plugin's, Rack's and the user's files, as :class:`pathlib.Path`. Available inside Rack only.

These are Rack's ``asset::plugin``, ``asset::system`` and ``asset::user``
(:mod:`rackpy.raw.asset`). Plugin and system files are read-only; write only
under :func:`user`, or in a module's patch storage.
"""

from pathlib import Path

from rackpy.raw import asset as _raw
from rackpy.raw import plugin as _plugin

__all__ = ["plugin", "system", "user"]


def plugin(filename: str = "") -> Path:
    """A file in this plugin's folder, e.g. ``plugin("res/table.json")``. Main thread only."""
    return Path(_raw.plugin(_plugin.plugin_instance(), filename))


def system(filename: str = "") -> Path:
    """A file in Rack's own read-only folder."""
    return Path(_raw.system(filename))


def user(filename: str = "") -> Path:
    """A file in Rack's user folder, which plugins may write."""
    return Path(_raw.user(filename))
