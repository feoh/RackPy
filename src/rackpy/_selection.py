# SPDX-License-Identifier: GPL-3.0-or-later
"""Name lookup for driver and device facades; IDs remain available through raw APIs."""

from collections.abc import Iterable


def by_name[T](items: Iterable[tuple[str, T]], name: str, kind: str) -> T:
    """Return the sole named item, or raise rather than selecting an ambiguous one."""
    matches = [item for candidate, item in items if candidate == name]
    if not matches:
        raise LookupError(f"{kind} {name!r} was not found")
    if len(matches) != 1:
        raise LookupError(f"{kind} {name!r} is ambiguous; select by ID")
    return matches[0]


def unique_names[T](items: Iterable[tuple[str, T]], kind: str) -> dict[str, T]:
    """Make a name map without silently dropping items that share a name."""
    result: dict[str, T] = {}
    for name, item in items:
        if name in result:
            raise LookupError(f"{kind} {name!r} is ambiguous; enumerate and select by ID")
        result[name] = item
    return result
