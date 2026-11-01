# SPDX-License-Identifier: GPL-3.0-or-later
"""The release check on the real manifest and inventory (RP-42): with everything in place it passes, and
deliberately removing a mapping, a test, a docs anchor, an exported name or a platform's passing test fails it.

The "everything in place" state is synthetic and in memory only: every bound record marked verified, every Python
name it claims exported, and every cited test passing. It shows the gate can open, and what closes it; it is not
evidence that RackPy meets the bar (that needs the real introspection, test outcomes and review)."""

import copy
from typing import Any

import binding_manifest as bm
import pytest

pytestmark = pytest.mark.unit

PLATFORM = "lin-x64"


@pytest.fixture(scope="module")
def ready() -> tuple[bm.Inputs, dict[str, Any], dict[str, Any], dict[str, str]]:
    inputs = bm.load_inputs()
    manifest = copy.deepcopy(bm.load_json(bm.MANIFEST))
    objects: dict[str, Any] = {}
    outcomes: dict[str, str] = {}
    for record in manifest["entries"]:
        if record["status"] in bm.BOUND:
            record.update(status="verified", reviewer="synthetic (tests/coverage/test_release_mutations.py)")
            for key in ("raw", "facade"):
                if name := (record.get("python") or {}).get(key):
                    objects[name] = {}
            outcomes.update(dict.fromkeys(record["tests"], "passed"))
    return inputs, manifest, {"objects": objects, "platform": PLATFORM}, outcomes


def problems(
    inputs: bm.Inputs, manifest: dict[str, Any], artifact: dict[str, Any] | None, outcomes: dict[str, str] | None
) -> set[str]:
    return {p.code for p in bm.release_problems(inputs, manifest, PLATFORM, artifact, outcomes)}


def first_bound(manifest: dict[str, Any]) -> dict[str, Any]:
    return next(r for r in manifest["entries"] if r["status"] in bm.BOUND and (r.get("python") or {}).get("raw"))


def test_the_gate_opens_only_with_everything_in_place(
    ready: tuple[bm.Inputs, dict[str, Any], dict[str, Any], dict[str, str]],
) -> None:
    inputs, manifest, artifact, outcomes = ready
    assert problems(inputs, manifest, artifact, outcomes) == set()
    assert "missing-runtime-artifact" in problems(inputs, manifest, None, outcomes)
    assert "missing-test-evidence" in problems(inputs, manifest, artifact, None)


@pytest.mark.parametrize(
    ("mutation", "expected"),
    [
        ("drop the record", "missing"),
        ("drop its tests", "tests"),
        ("cite a test that does not exist", "tests"),
        ("drop its docs", "docs"),
        ("cite a docs anchor that does not exist", "docs"),
        ("drop its Python name", "unbound"),
        ("leave it implemented, not verified", "incomplete"),
        ("verified without a reviewer", "reviewer"),
        ("its name missing from the package", "not-exported"),
        ("its tests failed on the platform", "evidence-failed"),
        ("its tests did not run on the platform", "no-evidence"),
    ],
)
def test_each_removal_closes_the_gate(
    ready: tuple[bm.Inputs, dict[str, Any], dict[str, Any], dict[str, str]], mutation: str, expected: str
) -> None:
    inputs, base, base_artifact, base_outcomes = ready
    manifest, artifact, outcomes = copy.deepcopy(base), copy.deepcopy(base_artifact), dict(base_outcomes)
    record = first_bound(manifest)
    tests = list(record["tests"])
    if mutation == "drop the record":
        manifest["entries"].remove(record)
    elif mutation == "drop its tests":
        record["tests"] = []
    elif mutation == "cite a test that does not exist":
        record["tests"] = [*tests, "tests/values/in_rack/test_common.py::test_no_such_test"]
    elif mutation == "drop its docs":
        record["docs"] = []
    elif mutation == "cite a docs anchor that does not exist":
        record["docs"] = ["docs/reference/values.md#no-such-anchor"]
    elif mutation == "drop its Python name":
        record["python"] = None
    elif mutation == "leave it implemented, not verified":
        record.update(status="implemented", reviewer=None)
    elif mutation == "verified without a reviewer":
        record["reviewer"] = None
    elif mutation == "its name missing from the package":
        del artifact["objects"][record["python"]["raw"]]
    elif mutation == "its tests failed on the platform":
        outcomes[tests[0]] = "failed"
    elif mutation == "its tests did not run on the platform":
        for t in tests:
            outcomes.pop(t, None)
    found = problems(inputs, manifest, artifact, outcomes)
    assert expected in found, (mutation, sorted(found))
