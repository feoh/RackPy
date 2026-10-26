# SPDX-License-Identifier: GPL-3.0-or-later
"""Files, directories and archives with ``pathlib`` paths, over :mod:`rackpy.raw.system`.

Everything here blocks the calling thread (with the GIL released while Rack works) and raises
Rack's ``Exception`` where Rack does. :func:`unarchive` checks an archive's entries before Rack
unpacks it, because Rack 2.6.6 follows ``../`` entries out of the target directory.
"""

import io
import os
import tarfile
from pathlib import Path, PurePosixPath
from typing import Any

from rackpy.raw import system as _system

__all__ = ["archive", "entries", "open_url", "read_bytes", "temp_directory", "unarchive", "write_bytes"]

_READ_ZSTD: Any = "r:zst"  # Python 3.14's tarfile reads Zstandard (typeshed's overloads do not list it yet)


def read_bytes(path: str | os.PathLike[str]) -> bytes:
    """The file's bytes (Rack's readFile)."""
    return _system.read_file(path)


def write_bytes(path: str | os.PathLike[str], data: bytes) -> None:
    """Writes the bytes, replacing the file (Rack's writeFile)."""
    _system.write_file(path, data)


def entries(directory: str | os.PathLike[str], depth: int = 0) -> list[Path]:
    """The directory's entries as paths (depth -1: all the way down)."""
    return [Path(p) for p in _system.get_entries(directory, depth)]


def temp_directory() -> Path:
    """The system's temporary directory."""
    return Path(_system.get_temp_directory())


def archive(
    directory: str | os.PathLike[str], to: str | os.PathLike[str] | None = None, level: int = 1
) -> bytes | None:
    """The directory's contents as a .tar.zst: written to ``to``, or returned as bytes."""
    if to is None:
        return _system.archive_directory_to_bytes(directory, level)
    _system.archive_directory(to, directory, level)
    return None


def _unsafe(name: str) -> bool:
    path = PurePosixPath(name)
    return path.is_absolute() or ".." in path.parts


def _check(source: str | os.PathLike[str] | bytes) -> None:
    raw = io.BytesIO(source) if isinstance(source, bytes) else Path(source).open("rb")  # noqa: SIM115  # closed below
    try:
        with tarfile.open(fileobj=raw, mode=_READ_ZSTD) as tar:
            for member in tar:
                if _unsafe(member.name):
                    raise ValueError(f"the archive's entry {member.name!r} would land outside the directory")
                if (member.issym() or member.islnk()) and _unsafe(
                    str(PurePosixPath(member.name).parent / member.linkname) if member.issym() else member.linkname
                ):
                    raise ValueError(f"the archive's link {member.name!r} points outside the directory")
    except tarfile.CompressionError as error:
        raise RuntimeError("cannot read the archive to check it; pass trust=True to unpack it unchecked") from error
    finally:
        raw.close()


def unarchive(
    source: str | os.PathLike[str] | bytes, directory: str | os.PathLike[str], *, trust: bool = False
) -> None:
    """Unpacks a .tar.zst (a path or bytes) into the directory, with Rack's unarchiver.

    Unless ``trust`` is set, the entries are checked first: an absolute or ``../`` path, or a link
    pointing out of the directory, raises ``ValueError`` before anything is written. As in Rack, a
    zero-byte file in the archive deletes the existing file.
    """
    if not trust:
        _check(source)
    _system.unarchive_to_directory(source, directory)


def open_url(url: str) -> None:
    """Opens the URL with the desktop's handler, without blocking (Rack's openBrowser)."""
    _system.open_browser(url)
