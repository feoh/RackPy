# SPDX-License-Identifier: GPL-3.0-or-later
"""C++ declaration -> raw name -> facade name stays discoverable in both directions (RP-32).

bindings/generated/mapping.json gives each Rack declaration its raw name and, when there is one, its facade name;
tests/facade/surface.json records what the facade actually exports. Neither may point at the other wrongly.
"""

import copy
import json
from collections import defaultdict
from pathlib import Path
from typing import Any

import pytest

pytestmark = pytest.mark.unit
REPO = Path(__file__).resolve().parents[2]

# Raw names the facade re-exports that are not Rack declarations, so the mapping has no entry for them.
NOT_RACK_DECLARATIONS = {
    "rackpy.raw.simd.Template": "RackPy's subscriptable adapter for C++ templates",
    "rackpy.raw.nanovg.NVGcolor": "NanoVG's colour struct, an external boundary type (nanovg.h)",
    "rackpy.raw._types.ValueQuantity": "RackPy's own natively stored Quantity",
    "rackpy.raw.widget.WidgetNode": "RackPy's checked detached-tree adapter",
}


def load() -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]]]:
    mapping = json.loads((REPO / "bindings" / "generated" / "mapping.json").read_text(encoding="utf-8"))
    surface = json.loads((REPO / "tests" / "facade" / "surface.json").read_text(encoding="utf-8"))
    return mapping["entries"], surface


def problems(entries: list[dict[str, Any]], surface: dict[str, dict[str, Any]]) -> list[str]:
    exported = {f"{module}.{name}" for module, names in surface.items() for name in names}

    def resolves(dotted: str) -> bool:  # a facade name, or a member of one (rackpy.math.Vec.x)
        parts = dotted.split(".")
        return any(".".join(parts[:i]) in exported for i in range(len(parts), 1, -1))

    found = [
        f"{e['id']}: facade {e['facade']} is not a public facade name"
        for e in entries
        if e["facade"] and not resolves(e["facade"])
    ]
    by_raw: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for e in entries:
        if e["raw"]:
            by_raw[e["raw"]].append(e)
    reexported: set[str] = set()
    for module, names in surface.items():
        for name, described in names.items():
            if not (isinstance(described, str) and described.startswith("re-export rackpy.raw.")):
                continue
            raw = described.split()[1]
            reexported.add(raw)
            if raw in NOT_RACK_DECLARATIONS:
                continue
            if raw not in by_raw:
                found.append(f"{module}.{name} re-exports {raw}, which no mapping entry names")
            elif not any(e["facade"] for e in by_raw[raw]):
                found.append(f"{module}.{name} re-exports {raw}, but no mapping entry for it records a facade")
    found += [f"allowlisted {raw} is no longer re-exported" for raw in NOT_RACK_DECLARATIONS if raw not in reexported]
    return found


def test_mapping_and_facade_agree() -> None:
    assert problems(*load()) == []


def test_broken_links_are_reported() -> None:
    entries, surface = load()
    entries = copy.deepcopy(entries)
    message = next(e for e in entries if e["id"] == "record:rack::midi::Message")
    for e in entries:
        if e["raw"] == message["raw"]:
            e["facade"] = None
    vec = next(e for e in entries if e["id"] == "record:rack::math::Vec")
    vec["facade"] = "rackpy.math.NoSuchVec"
    found = problems(entries, surface)
    assert any("rackpy.midi.Message re-exports rackpy.raw.midi.Message, but no mapping entry" in p for p in found)
    assert any("facade rackpy.math.NoSuchVec is not a public facade name" in p for p in found)
