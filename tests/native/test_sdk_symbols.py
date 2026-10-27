# SPDX-License-Identifier: GPL-3.0-or-later
"""Declarations the manifest maps to nothing because Rack never defines them (completeness policy 14).

A record whose notes start ``Declared but never defined`` claims that the
pinned libRack.so exports no symbol for the declaration, so no plugin can call
it. This checks the claim against the SDK's library, with Rack's neighbouring
methods as positive controls.
"""

import json
import re
import subprocess
from pathlib import Path

import pytest

from tests.support import REPO, linux_x64_only, require_tools

pytestmark = [pytest.mark.native, linux_x64_only()]
UNDEFINED = "Declared but never defined"


def exported(rack_sdk: Path) -> str:
    require_tools("nm")
    return subprocess.run(
        ["nm", "-DC", "--defined-only", str(rack_sdk / "libRack.so")], capture_output=True, text=True, check=True
    ).stdout


def test_declared_but_undefined_functions_have_no_symbol(rack_sdk: Path) -> None:
    symbols = exported(rack_sdk)
    manifest = json.loads((REPO / "bindings" / "manifest.json").read_text())
    records = [r for r in manifest["entries"] if (r["notes"] or "").startswith(UNDEFINED)]
    assert [r["id"] for r in records] == ["method:rack::audio::Port::getDeviceDetail(int, int)"]
    for record in records:
        assert record["python"]["kind"] == "no_runtime_meaning" and not record["python"]["raw"]
        name = re.match(r"method:([\w:]+)\(", record["id"])
        assert name, record["id"]
        assert f"{name.group(1)}(" not in symbols and f"{name.group(1)}[" not in symbols, record["id"]
    # Positive controls: the methods beside it are exported, so the search would find it if it were.
    for control in ("rack::audio::Port::getDeviceName", "rack::audio::Port::getDeviceNumInputs(int)"):
        assert control in symbols, control
