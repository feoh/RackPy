# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-40: no two of RackPy's native sources declare a same-named class in an anonymous namespace.

Off libstdc++, pybind11 identifies C++ types by typeid(T).name() (detail/internals.h, type_hash and
type_equal_to), and a class in an anonymous namespace has the same mangled name in every file that
declares one: `(anonymous namespace)::DriverView` from bindings/audio.cpp and from bindings/midi.cpp
were one type to it on macOS, so importing _rackpy_host failed with 'generic_type: type "Driver" is
already registered!'. Linux's libstdc++ compares such types by address, so only macOS showed it; this
check holds on every platform. The test programs in native/tests are separate executables and exempt.
"""

import re
from collections import defaultdict

import pytest

from tests.support import REPO

pytestmark = pytest.mark.unit

NATIVE = REPO / "native"
ANONYMOUS = re.compile(r"\bnamespace\s*\{")
DECLARATION = re.compile(r"^\s*(?:template\s*<[^>]*>\s*)?(?:struct|class)\s+([A-Za-z_]\w*)\b(?!\s*;)", re.MULTILINE)


def anonymous_bodies(source: str) -> list[str]:
    """The text of each top-level anonymous namespace (by brace matching; comments and strings with braces
    in them are rare enough in this code base not to matter here)."""
    bodies = []
    for match in ANONYMOUS.finditer(source):
        depth, end = 1, match.end()
        while depth and end < len(source):
            depth += {"{": 1, "}": -1}.get(source[end], 0)
            end += 1
        bodies.append(source[match.end() : end])
    return bodies


def local_types() -> dict[str, set[str]]:
    found: dict[str, set[str]] = defaultdict(set)
    for path in sorted(NATIVE.rglob("*.cpp")):
        if path.is_relative_to(NATIVE / "tests"):
            continue
        for body in anonymous_bodies(path.read_text(errors="replace")):
            for name in DECLARATION.findall(body):
                found[name].add(path.relative_to(REPO).as_posix())
    return found


def test_anonymous_namespace_class_names_are_unique_across_the_native_sources() -> None:
    found = local_types()
    assert found, "the scan found no local types at all; it no longer matches the sources"
    shared = {name: sorted(files) for name, files in found.items() if len(files) > 1}
    assert not shared, f"rename these so that each is unique (prefix it with its file's subject): {shared}"


def test_the_scan_sees_a_known_local_type() -> None:
    assert "native/bindings/audio.cpp" in local_types()["AudioDriverView"]
