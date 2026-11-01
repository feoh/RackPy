# SPDX-License-Identifier: GPL-3.0-or-later
"""The dependency constants Rack events use, compared to the pinned SDK headers."""

import json
import subprocess
import sys
from pathlib import Path

from rackpy.raw import blendish, glfw, ui

REPO = Path(__file__).resolve().parents[3]


def test_boundary_constants_match_compiled_sdk(tmp_path: Path) -> None:
    inventory = json.loads((REPO / "upstream" / "api-surface.json").read_text())
    entries = [e for e in inventory["entries"] if e["kind"] == "boundary_constant"]
    assert entries and any(e["name"] == "BND_CORNER_ALL" for e in entries)
    # This runs inside Rack, which cannot import the repository's tests package: tests.support.bin_command, inline.
    rack_sdk = str(REPO / "bin" / "rack-sdk")
    command = ["uv", "run", "--script", rack_sdk] if sys.platform == "win32" else [rack_sdk]
    sdk = Path(subprocess.check_output([*command, "path"], text=True).strip())
    # One static_assert per constant, comparing the binding's value with the SDK's headers as compiled here:
    # nothing to link or run, so it holds on every platform's compiler (RP-42; a linked program needed libRack).
    lines = ["#include <rack.hpp>"]
    for entry in entries:
        module = glfw if entry["boundary"] == "input" else blendish
        value = getattr(module, entry["name"])
        lines.append(f'static_assert(static_cast<int>({entry["name"]}) == {value}, "{entry["name"]} is not {value}");')
    source = tmp_path / "constants.cpp"
    source.write_text("\n".join(lines) + "\n")
    windows = ["-D_USE_MATH_DEFINES"] if sys.platform == "win32" else []
    compiled = subprocess.run(
        [
            "c++",
            "-std=c++11",
            "-fsyntax-only",
            *windows,
            f"-I{sdk / 'include'}",
            f"-I{sdk / 'dep' / 'include'}",
            str(source),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert compiled.returncode == 0, compiled.stderr[-4000:]
    assert len(lines) - 1 == len(entries)
    menu = ui.Menu()
    try:
        menu.corner_flags = blendish.BND_CORNER_TOP_LEFT | blendish.BND_CORNER_DOWN_RIGHT
        assert menu.corner_flags == 5
    finally:
        menu.close()
