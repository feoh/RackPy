# rackpy-example: runs-in=rack
"""Define a file-chooser callback for a module's menu or button."""

from pathlib import Path

from rackpy.dialog import open_file


def choose_sample() -> Path | None:
    """Call this from a Rack UI callback, not from an engine or audio callback."""
    return open_file(filters="Audio:wav,aiff,flac")
