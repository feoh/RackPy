# SPDX-License-Identifier: GPL-3.0-or-later
"""Declarations the manifest maps to nothing because Rack never defines them (completeness policy 14).

A record whose notes start ``Declared but never defined`` claims that the
pinned libRack exports no symbol for the declaration, so no plugin can call
it. This checks the claim against this platform's SDK library, with Rack's
neighbouring methods as positive controls, compared as mangled names
(tests.support.nested_name).
"""

import json
import re
from pathlib import Path

import pytest

from tests.support import MACOS, REPO, WINDOWS, nested_name, rack_platforms, require_tools, symbol_names

pytestmark = [pytest.mark.native, rack_platforms()]
UNDEFINED = "Declared but never defined"


def exported(rack_sdk: Path) -> str:
    require_tools("nm")
    # On Windows the SDK has libRack's import library, which lists what libRack.dll exports.
    return symbol_names(rack_sdk / ("libRack.dylib" if MACOS else "libRack.dll.a" if WINDOWS else "libRack.so"))


def test_declared_but_undefined_functions_have_no_symbol(rack_sdk: Path) -> None:
    symbols = exported(rack_sdk)
    manifest = json.loads((REPO / "bindings" / "manifest.json").read_text())
    records = [r for r in manifest["entries"] if (r["notes"] or "").startswith(UNDEFINED)]
    assert [r["id"] for r in records] == ["method:rack::audio::Port::getDeviceDetail(int, int)"]
    for record in records:
        assert record["python"]["kind"] == "no_runtime_meaning" and not record["python"]["raw"]
        name = re.match(r"method:([\w:]+)\(", record["id"])
        assert name, record["id"]
        assert not nested_name(name.group(1)).search(symbols), record["id"]
    # Positive controls: the methods beside it are exported, so the search would find it if it were.
    for control in ("rack::audio::Port::getDeviceName", "rack::audio::Port::getDeviceNumInputs"):
        assert nested_name(control).search(symbols), control
