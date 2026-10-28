# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-31: mutation tests of the coverage checks, on the real manifest and inventory.

Each test removes or breaks one overload or callback mapping (a record, the
Python name the built package exports for it, its Python mapping, its
callback strategy or its row in the generated callback inventory) and
asserts that the checks fail for exactly that declaration. The built
package's names come from a synthetic introspection holding every name the
manifest claims; the in-Rack test tests/coverage/test_introspection.py runs
the same check against what the built plugin really exports.
"""

import collections
import copy
from typing import Any

import binding_manifest as bm
import generate_bindings as gb
import pytest

pytestmark = pytest.mark.unit


@pytest.fixture(scope="module")
def real() -> tuple[bm.Inputs, dict[str, Any]]:
    return bm.load_inputs(), bm.load_json(bm.MANIFEST)


def exported(manifest: dict[str, Any]) -> dict[str, Any]:
    names: dict[str, Any] = {}
    for r in manifest["entries"]:
        for key in ("raw", "facade"):
            value = (r["python"] or {}).get(key)
            if value:
                names[value] = {"kind": "function"}
    return {"objects": names}


def problems_for(inputs: bm.Inputs, manifest: dict[str, Any], introspection: dict[str, Any]) -> set[tuple[str, str]]:
    return {(p.code, p.id) for p in bm.validate(inputs, manifest, introspection)}


def test_deprecation_warning_evidence_is_required(real: tuple[bm.Inputs, dict[str, Any]]) -> None:
    inputs, manifest = real
    victim = "alias:rack::widget::SVGWidget"
    mutated_inputs = copy.deepcopy(inputs)
    del mutated_inputs.deprecation_evidence[victim]
    assert ("deprecation-evidence", victim) in problems_for(mutated_inputs, manifest, exported(manifest))

    mutated = copy.deepcopy(manifest)
    record = next(r for r in mutated["entries"] if r["id"] == victim)
    record["tests"].remove(inputs.deprecation_evidence[victim])
    assert ("deprecation-evidence", victim) in problems_for(inputs, mutated, exported(manifest))

    mutated_inputs = copy.deepcopy(inputs)
    mutated_inputs.deprecation_evidence[victim] = "tests/no_such_warning_test.py::test_missing"
    assert ("deprecation-evidence", victim) in problems_for(mutated_inputs, manifest, exported(manifest))


def overload_groups(manifest: dict[str, Any]) -> list[list[dict[str, Any]]]:
    """Python names that map several C++ overloads (bound records only), largest first."""
    groups: dict[str, list[dict[str, Any]]] = collections.defaultdict(list)
    for r in manifest["entries"]:
        raw = (r["python"] or {}).get("raw")
        if r["status"] in bm.BOUND and raw and r["id"].startswith(("method:", "function:", "constructor:")):
            groups[raw].append(r)
    found = [g for g in groups.values() if len(g) > 1]
    return sorted(found, key=lambda g: (-len(g), g[0]["id"]))


def test_the_real_manifest_passes_with_every_name_exported(real: tuple[bm.Inputs, dict[str, Any]]) -> None:
    inputs, manifest = real
    assert bm.validate(inputs, manifest, exported(manifest)) == []
    assert len(overload_groups(manifest)) >= 50  # enough overload groups for the mutations below to mean something


def test_removing_overload_records_is_caught(real: tuple[bm.Inputs, dict[str, Any]]) -> None:
    """One overload of each of ten overloaded names removed: exactly those ten records are missing."""
    inputs, manifest = real
    victims = {group[-1]["id"] for group in overload_groups(manifest)[:10]}
    mutated = copy.deepcopy(manifest)
    mutated["entries"] = [r for r in mutated["entries"] if r["id"] not in victims]
    assert problems_for(inputs, mutated, exported(manifest)) == {("missing", v) for v in victims}


def test_dropping_overloaded_names_from_the_built_package_flags_every_overload(
    real: tuple[bm.Inputs, dict[str, Any]],
) -> None:
    """Ten overloaded names missing from the built package: every overload of each is flagged, and nothing else."""
    inputs, manifest = real
    groups = overload_groups(manifest)[:10]
    introspection = exported(manifest)
    dropped = {group[0]["python"]["raw"] for group in groups}
    for name in dropped:
        del introspection["objects"][name]
    flagged = problems_for(inputs, manifest, introspection)
    assert {("not-exported", r["id"]) for group in groups for r in group} <= flagged
    # Nothing else: every flagged record maps one of the dropped names (aliases and facades may share them).
    mapping = {r["id"]: set((r["python"] or {}).values()) for r in manifest["entries"]}
    assert all(code == "not-exported" and mapping[ident] & dropped for code, ident in flagged)


def callback_records(manifest: dict[str, Any], strategy: str) -> list[dict[str, Any]]:
    return [
        r
        for r in manifest["entries"]
        if r["callback"] == strategy and r["status"] in bm.BOUND and (r["python"] or {}).get("raw")
    ]


STRATEGIES = ("ui", "engine", "driver", "mixed")


def test_unmapping_callbacks_is_caught(real: tuple[bm.Inputs, dict[str, Any]]) -> None:
    """One callback of each strategy loses its Python mapping, another gets a wrong strategy: all are caught."""
    inputs, manifest = real
    unmapped = {s: callback_records(manifest, s)[0]["id"] for s in STRATEGIES}
    restrategized = {s: callback_records(manifest, s)[1]["id"] for s in STRATEGIES}
    mutated = copy.deepcopy(manifest)
    by_id = {r["id"]: r for r in mutated["entries"]}
    for victim in unmapped.values():
        by_id[victim]["python"] = None
    for strategy, victim in restrategized.items():
        by_id[victim]["callback"] = "ui" if strategy != "ui" else "engine"
    found = problems_for(inputs, mutated, exported(manifest))
    assert {("unbound", v) for v in unmapped.values()} <= found
    assert {("callback", v) for v in restrategized.values()} <= found
    assert {ident for _, ident in found} == set(unmapped.values()) | set(restrategized.values())


def test_the_generated_callback_inventory_follows_a_removed_mapping(real: tuple[bm.Inputs, dict[str, Any]]) -> None:
    inputs, manifest = real
    victim = callback_records(manifest, "ui")[0]
    before = gb.callback_paths(manifest, inputs)
    mutated = copy.deepcopy(manifest)
    target = next(r for r in mutated["entries"] if r["id"] == victim["id"])
    target["status"] = "planned"
    target["notes"] = "Responsible task: RP-31 mutation test."
    after = gb.callback_paths(mutated, inputs)
    assert before != after  # so the committed file no longer matches (generate_bindings.py check fails)
    row = next(line for line in after.splitlines() if f"`{victim['id']}`" in line)
    assert "**Full-release blocker**" in row
