# SPDX-License-Identifier: GPL-3.0-or-later
"""The release check on the real manifest and inventory (RP-42): with everything in place it passes, and
deliberately removing a mapping, a test, a docs anchor, an exported name or a platform's passing test fails it.

The "everything in place" state is synthetic and in memory only: every bound record marked verified, every Python
name it claims exported, and every cited test passing. It shows the gate can open, and what closes it; it is not
evidence that RackPy meets the bar (that needs the real introspection, test outcomes and review)."""

import copy
import dataclasses
from typing import Any

import binding_manifest as bm
import pytest

pytestmark = pytest.mark.unit

PLATFORM = "lin-x64"


@pytest.fixture(scope="module")
def ready() -> tuple[bm.Inputs, dict[str, Any], dict[str, Any], dict[str, str]]:
    inputs = bm.load_inputs()
    manifest = copy.deepcopy(bm.load_json(bm.MANIFEST))
    reviewer = "agent-review:synthetic (tests/coverage/test_release_mutations.py)"
    inputs.reviews = {}
    objects: dict[str, Any] = {}
    outcomes: dict[str, str] = {}
    for record in manifest["entries"]:
        if record["status"] in bm.BOUND:
            record.update(status="verified", reviewer=reviewer)
            inputs.reviews[record["id"]] = {reviewer}
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


def test_platform_scoped_known_gap_keeps_other_platform_mapping_intact(
    ready: tuple[bm.Inputs, dict[str, Any], dict[str, Any], dict[str, str]],
) -> None:
    inputs, base_manifest, _, _ = ready
    manifest = copy.deepcopy(base_manifest)
    record = next(r for r in manifest["entries"] if r["id"].startswith("function:rack::system::runProcessDetached"))
    record.update(
        status="implemented",
        notes="Known release gap [win-x64]: UAC elevation would block the UI thread.",
    )
    assert bm.known_gap(record, "win-x64")
    assert not bm.known_gap(record, "lin-x64")
    assert bm.known_gap_marker_problem(record, inputs.repo, "win-x64") is None
    assert bm.known_gap_marker_problem(record, inputs.repo, "lin-x64") is None
    assert bm.known_gap_marker_problem({**record, "id": "unlisted-gap"}, inputs.repo, "win-x64")


def test_platform_scoped_known_gap_is_not_unexplained_missing_evidence(
    ready: tuple[bm.Inputs, dict[str, Any], dict[str, Any], dict[str, str]],
) -> None:
    inputs, manifest, _, outcomes = ready
    record = next(r for r in manifest["entries"] if r["id"].startswith("function:rack::system::runProcessDetached"))
    outcomes[record["tests"][0]] = "skipped"

    problems, counts = bm.evidence_problems(inputs, manifest, "win-x64", outcomes)
    assert not any(p.id == record["id"] for p in problems)
    assert counts["known gap"] == 1
    assert counts["no evidence"] == 0

    record["notes"] = record["notes"].replace("Known release gap [win-x64]:", "")
    problems, counts = bm.evidence_problems(inputs, manifest, "win-x64", outcomes)
    assert any(p.id == record["id"] and p.code == "no-evidence" for p in problems)
    assert counts["known gap"] == 0
    assert counts["no evidence"] == 1


def test_owner_approved_known_gaps_pass_only_when_documented(
    ready: tuple[bm.Inputs, dict[str, Any], dict[str, Any], dict[str, str]],
) -> None:
    inputs, manifest, artifact, outcomes = ready
    # The approved listed gaps are accepted for the partial-release gate, but do not become bindings.
    assert not ({"incomplete", "owner-review", "known-gap"} & problems(inputs, manifest, artifact, outcomes))
    gap = next(r for r in manifest["entries"] if r["status"] == "unrepresentable")
    notes = gap["notes"]
    gap["notes"] = notes.replace(bm.KNOWN_GAP_OPEN, "")
    assert "incomplete" in problems(inputs, manifest, artifact, outcomes)
    gap["notes"] = notes
    assert bm.known_gap_marker_problem({**gap, "id": "unlisted-test-gap"}, inputs.repo)
    assert "missing-runtime-artifact" in problems(inputs, manifest, None, outcomes)
    assert "missing-test-evidence" in problems(inputs, manifest, artifact, None)


def test_guard_only_sample_skip_is_scoped_to_named_non_linux_platforms() -> None:
    examples = bm.load_json(bm.EXAMPLES)["samples"]
    sample = next(s for s in examples if s["id"] == "guide-07")
    disposition = sample["disposition"]
    guard = disposition["instrument_tests"]
    guard_test = next(iter(guard))
    outcomes = {test: "passed" for test in disposition["tests"]}
    outcomes[guard_test] = "skipped"

    problems, results = bm.sample_problems([sample], "mac-arm64", outcomes)
    assert problems == []
    assert results[sample["id"]]["status"] == "passed"
    assert results[sample["id"]]["tests"][guard_test].startswith("passed (guarded;")

    problems, _ = bm.sample_problems([sample], "lin-x64", outcomes)
    assert any(p.code == "sample-skipped" for p in problems)
    outcomes[guard_test] = "failed"
    problems, _ = bm.sample_problems([sample], "mac-x64", outcomes)
    assert any(p.code == "sample-failed" for p in problems)


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
        ("no verdict file accepts it", "reviewer"),
        ("verified in another reviewer's name", "reviewer"),
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
    elif mutation == "no verdict file accepts it":
        inputs = dataclasses.replace(inputs, reviews={k: v for k, v in inputs.reviews.items() if k != record["id"]})
    elif mutation == "verified in another reviewer's name":
        record["reviewer"] = "agent-review:someone else"
    elif mutation == "its name missing from the package":
        del artifact["objects"][record["python"]["raw"]]
    elif mutation == "its tests failed on the platform":
        outcomes[tests[0]] = "failed"
    elif mutation == "its tests did not run on the platform":
        for t in tests:
            outcomes.pop(t, None)
    found = problems(inputs, manifest, artifact, outcomes)
    assert expected in found, (mutation, sorted(found))
