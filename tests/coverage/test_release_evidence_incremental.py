# SPDX-License-Identifier: GPL-3.0-or-later
"""Incremental release-review evidence is tied to exact Git inputs and reviewer records."""

import importlib.machinery
import importlib.util
import json
import subprocess
from pathlib import Path
from typing import Any

import pytest

from tests.support import BIN

pytestmark = pytest.mark.unit
RECORD_ID = "method:rack::Example::value()"


def load_script() -> Any:
    loader = importlib.machinery.SourceFileLoader("release_evidence_incremental", str(BIN / "release-evidence"))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


@pytest.fixture
def evidence() -> Any:
    module = load_script()
    original_repo = module.bm.REPO
    yield module
    module.bm.REPO = original_repo


def commit(root: Path, message: str) -> str:
    subprocess.run(["git", "-C", str(root), "add", "-A"], check=True)
    subprocess.run(
        [
            "git",
            "-C",
            str(root),
            "-c",
            "commit.gpgsign=false",
            "-c",
            "user.name=Test",
            "-c",
            "user.email=test@example.invalid",
            "commit",
            "-m",
            message,
        ],
        check=True,
        capture_output=True,
    )
    return subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"], check=True, text=True, capture_output=True
    ).stdout.strip()


def make_repo(root: Path, *, note: str = "same", source: bytes = b"source", test: bytes = b"test") -> str:
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    (root / "bindings").mkdir()
    (root / "native").mkdir()
    (root / "tests").mkdir()
    record = {
        "id": RECORD_ID,
        "status": "implemented",
        "implementation": "native/example.cpp",
        "tests": ["tests/test_example.py::test_value"],
        "notes": note,
    }
    (root / "bindings" / "manifest.json").write_text(json.dumps({"entries": [record]}, indent=2) + "\n")
    (root / "native" / "example.cpp").write_bytes(source)
    (root / "tests" / "test_example.py").write_bytes(test)
    return commit(root, "base")


def write_verdict(path: Path, commit_sha: str, reviewer: str = "agent-review:run-1") -> None:
    path.write_text(
        json.dumps(
            {
                "slice": "sample-01",
                "reviewer": reviewer,
                "commit": commit_sha,
                "records": {RECORD_ID: {"verdict": "accept"}},
            }
        )
    )


def test_carry_retains_only_unchanged_accepted_records(evidence: Any, tmp_path: Path) -> None:
    root = tmp_path / "repo"
    old = make_repo(root)
    evidence.bm.REPO = root
    review = tmp_path / "review"
    review.mkdir()
    source = review / "sample-01.verdicts.json"
    write_verdict(source, old)
    (root / "unrelated.txt").write_text("not a mapping input")
    new = commit(root, "unrelated edit")

    plan_path = review / "carry.json"
    evidence.carry(old, new, review_dir=review, dest=plan_path)
    plan = json.loads(plan_path.read_text())
    carried = plan["carried"][RECORD_ID]
    assert carried["reviewer"] == "agent-review:run-1"
    assert carried["reviewed_commit"] == old
    assert carried["carry_commit"] == new
    assert carried["source_file"] == source.name
    assert carried["source_sha256"] == evidence.sha256(source)
    assert plan["redo"] == []
    assert plan["chunks"] == []
    docs, redo, chunks = evidence.carried_review_docs(plan_path, new)
    assert docs[0]["records"] == {RECORD_ID: {"verdict": "accept"}}
    assert redo == set()
    assert chunks == []


@pytest.mark.parametrize("changed", ["manifest", "test", "source"])
def test_changed_manifest_test_or_mapping_source_goes_to_redo(evidence: Any, tmp_path: Path, changed: str) -> None:
    root = tmp_path / "repo"
    old = make_repo(root)
    evidence.bm.REPO = root
    review = tmp_path / "review"
    review.mkdir()
    write_verdict(review / "sample-01.verdicts.json", old)
    target = {
        "manifest": root / "bindings" / "manifest.json",
        "test": root / "tests" / "test_example.py",
        "source": root / "native" / "example.cpp",
    }[changed]
    if changed == "manifest":
        manifest = json.loads(target.read_text())
        manifest["entries"][0]["notes"] = "edited mapping note"
        target.write_text(json.dumps(manifest, indent=2) + "\n")
    else:
        target.write_bytes(target.read_bytes() + b" changed")
    new = commit(root, "change a cited input")

    plan_path = review / "carry.json"
    evidence.carry(old, new, review_dir=review, dest=plan_path)
    plan = json.loads(plan_path.read_text())
    assert plan["carried"] == {}
    assert plan["redo"] == [RECORD_ID]
    assert plan["chunks"] == [[RECORD_ID]]


def test_carry_rounds_preserve_original_reviewer_and_commit(evidence: Any, tmp_path: Path) -> None:
    root = tmp_path / "repo"
    old = make_repo(root)
    evidence.bm.REPO = root
    review = tmp_path / "review"
    review.mkdir()
    write_verdict(review / "sample-01.verdicts.json", old)
    first_plan = review / "first-carry.json"
    evidence.carry(old, old, review_dir=review, dest=first_plan)
    (root / "unrelated.txt").write_text("first candidate")
    first = commit(root, "first candidate")
    first_candidate_plan = review / "carry-candidate.json"
    evidence.carry(old, first, review_dir=review, dest=first_candidate_plan)
    docs, redo, _ = evidence.carried_review_docs(first_candidate_plan, first)
    assert not redo
    assert docs[0]["records"][RECORD_ID]["verdict"] == "accept"
    (root / "another.txt").write_text("next candidate")
    second = commit(root, "second candidate")

    second_plan = review / "second-carry.json"
    evidence.carry(first, second, review_dir=review, dest=second_plan)
    carried = json.loads(second_plan.read_text())["carried"][RECORD_ID]
    assert carried["reviewed_commit"] == old
    assert carried["reviewer"] == "agent-review:run-1"
    assert carried["carry_commit"] == second


def test_forged_carry_commit_and_changed_source_verdict_are_rejected(evidence: Any, tmp_path: Path) -> None:
    root = tmp_path / "repo"
    old = make_repo(root)
    evidence.bm.REPO = root
    review = tmp_path / "review"
    review.mkdir()
    source = review / "sample-01.verdicts.json"
    write_verdict(source, old)
    (root / "unrelated.txt").write_text("candidate")
    new = commit(root, "candidate")
    plan_path = review / "carry.json"
    evidence.carry(old, new, review_dir=review, dest=plan_path)
    plan = json.loads(plan_path.read_text())
    plan["carried"][RECORD_ID]["carry_commit"] = "0" * 40
    plan_path.write_text(json.dumps(plan))
    with pytest.raises(SystemExit, match="invalid or changed carry provenance"):
        evidence.carried_review_docs(plan_path, new)

    plan["carried"][RECORD_ID]["carry_commit"] = new
    plan_path.write_text(json.dumps(plan))
    source.write_text(source.read_text() + " ")
    with pytest.raises(SystemExit, match="source verdict file changed or is missing"):
        evidence.carried_review_docs(plan_path, new)


def test_verdicts_reject_placeholder_and_nonfrozen_outputs(evidence: Any, tmp_path: Path) -> None:
    review = tmp_path / "review"
    review.mkdir()
    slice_id = "redo-001"
    (review / "slices.json").write_text(
        json.dumps({"commit": "a" * 40, "size": 25, "slices": [{"slice": slice_id, "records": [RECORD_ID]}]})
    )
    answer = tmp_path / "answer.md"
    answer.write_text(
        "```json\n"
        + json.dumps(
            {
                "slice": slice_id,
                "reviewer": "agent-review:run-1",
                "commit": "b" * 40,
                "records": {RECORD_ID: {"verdict": "finding", "finding": "TBD"}},
            }
        )
        + "\n```\n"
    )
    with pytest.raises(SystemExit, match="frozen slices commit"):
        evidence.verdicts([answer], dest=review)

    answer.write_text(answer.read_text().replace("b" * 40, "a" * 40))
    with pytest.raises(SystemExit, match="placeholder"):
        evidence.verdicts([answer], dest=review)


def test_slices_from_carry_plan_are_exact_and_limited_to_25(evidence: Any, tmp_path: Path) -> None:
    plan = {
        "commit": "a" * 40,
        "chunks": [[f"method:rack::Example::{index}()" for index in range(25)], ["method:rack::Example::last()"]],
    }
    plan_path = tmp_path / "carry.json"
    plan_path.write_text(json.dumps(plan))
    evidence.slices(size=25, dest=tmp_path / "review", carry_plan=plan_path)
    data = json.loads((tmp_path / "review" / "slices.json").read_text())
    assert [len(item["records"]) for item in data["slices"]] == [25, 1]
    with pytest.raises(SystemExit, match="between 1 and 25"):
        evidence.slices(size=26, dest=tmp_path / "review")


def test_collect_allow_promoted_only_accepts_manifest_and_generated_outputs(evidence: Any, tmp_path: Path) -> None:
    root = tmp_path / "repo"
    subprocess.run(["git", "init", "-q", str(root)], check=True)
    (root / "bindings").mkdir()
    (root / "bindings" / "manifest.json").write_text("{}\n")
    base = commit(root, "base")
    evidence.bm.REPO = root
    (root / "bindings" / "manifest.json").write_text('{"promoted": true}\n')
    assert evidence.check_checkout(base, allow_promoted=True) is None
    (root / "unrelated.py").write_text("changed")
    subprocess.run(["git", "-C", str(root), "add", "unrelated.py"], check=True)
    with pytest.raises(SystemExit, match="only manifest/generated outputs"):
        evidence.check_checkout(base, allow_promoted=True)
