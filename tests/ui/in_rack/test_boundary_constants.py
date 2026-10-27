# SPDX-License-Identifier: GPL-3.0-or-later
"""The dependency constants Rack events use, compared to the pinned SDK headers."""

import json
import subprocess
from pathlib import Path

from rackpy.raw import blendish, glfw, ui

REPO = Path(__file__).resolve().parents[3]


def test_boundary_constants_match_compiled_sdk(tmp_path: Path) -> None:
    inventory = json.loads((REPO / "upstream" / "api-surface.json").read_text())
    entries = [e for e in inventory["entries"] if e["kind"] == "boundary_constant"]
    assert entries and any(e["name"] == "BND_CORNER_ALL" for e in entries)
    sdk = Path(subprocess.check_output([str(REPO / "bin" / "rack-sdk"), "path"], text=True).strip())
    lines = ["#include <rack.hpp>", "#include <cstdio>", "int main() {"]
    lines.extend(f'printf("{e["name"]} %d\\n", static_cast<int>({e["name"]}));' for e in entries)
    lines.append("}")
    source, binary = tmp_path / "constants.cpp", tmp_path / "constants"
    source.write_text("\n".join(lines))
    subprocess.run(
        [
            "c++",
            "-std=c++11",
            f"-I{sdk / 'include'}",
            f"-I{sdk / 'dep' / 'include'}",
            str(source),
            f"-L{sdk}",
            "-lRack",
            f"-Wl,-rpath,{sdk}",
            "-o",
            str(binary),
        ],
        check=True,
    )
    values = dict(line.split() for line in subprocess.check_output([str(binary)], text=True).splitlines())
    assert len(values) == len(entries)
    for entry in entries:
        module = glfw if entry["boundary"] == "input" else blendish
        assert getattr(module, entry["name"]) == int(values[entry["name"]])
    menu = ui.Menu()
    try:
        menu.corner_flags = blendish.BND_CORNER_TOP_LEFT | blendish.BND_CORNER_DOWN_RIGHT
        assert menu.corner_flags == 5
    finally:
        menu.close()
