# SPDX-License-Identifier: GPL-3.0-or-later
"""Release evidence must be judged against the exact clean candidate tested in CI."""

import json
import runpy
import subprocess
from pathlib import Path
from typing import Any

import pytest

pytestmark = pytest.mark.unit
REPO = Path(__file__).resolve().parents[2]


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=repo, check=True, capture_output=True, text=True).stdout.strip()


@pytest.fixture
def candidate(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> tuple[Path, dict[str, Any], Any]:
    git(tmp_path, "init", "-q")
    (tmp_path / "input").write_text("candidate\n", encoding="utf-8")
    git(tmp_path, "add", "input")
    git(tmp_path, "-c", "user.name=Test", "-c", "user.email=test@example.org", "commit", "-qm", "candidate")
    head = git(tmp_path, "rev-parse", "HEAD")
    namespace = runpy.run_path(str(REPO / "bin/release-evidence"))
    monkeypatch.setattr(namespace["bm"], "REPO", tmp_path)
    info = {"status": "completed", "headSha": head, "headBranch": "candidate", "url": "https://example.org", "jobs": []}
    check = namespace["checked_run"]
    monkeypatch.setitem(check.__globals__, "gh", lambda *args: json.dumps(info))
    return tmp_path, info, check


@pytest.mark.parametrize("conclusion", [None, "failure", "cancelled", "skipped"])
def test_shared_checker_job_must_succeed(conclusion: str | None) -> None:
    namespace = runpy.run_path(str(REPO / "bin/release-evidence"))
    check = namespace["shared_check_problems"]
    job = namespace["SHARED_JOB"]
    assert check({job: "success"}) == []
    problems = check({} if conclusion is None else {job: conclusion})
    assert len(problems) == 1
    assert problems[0].code == "shared-checks"


@pytest.mark.parametrize("platform", ["mac-arm64", "mac-x64", "win-x64"])
def test_full_suite_selection_is_required(tmp_path: Path, platform: str) -> None:
    namespace = runpy.run_path(str(REPO / "bin/release-evidence"))
    check = namespace["selection_problems"]
    selections = [tmp_path / artifact / "selection.txt" for artifact in namespace["TESTS"][platform]]
    assert check(tmp_path, "lin-x64") == []
    assert check(tmp_path, platform)[0].code == "test-selection"  # absent shard reports are not a full run
    for selection in selections:
        selection.parent.mkdir(parents=True)
        selection.write_text("", encoding="utf-8")
    assert check(tmp_path, platform) == []
    selections[0].write_text("engine", encoding="utf-8")
    assert check(tmp_path, platform)[0].code == "test-selection"


def test_review_verdict_must_match_exact_ci_candidate(candidate: tuple[Path, dict[str, Any], Any]) -> None:
    _, info, check = candidate
    require = check.__globals__["require_verdict_commit"]
    path = Path("widgets-01.verdicts.json")
    require(path, {"commit": info["headSha"]}, info["headSha"])
    with pytest.raises(SystemExit, match="not CI candidate"):
        require(path, {"commit": "0" * 40}, info["headSha"])


def test_run_must_be_finished_and_match_checkout(candidate: tuple[Path, dict[str, Any], Any]) -> None:
    _, info, check = candidate
    assert check(123) == info
    info["status"] = "in_progress"
    with pytest.raises(SystemExit, match="finished run"):
        check(123)
    info["status"] = "completed"
    info["headSha"] = "0" * 40
    with pytest.raises(SystemExit, match="exact candidate commit"):
        check(123)


@pytest.mark.parametrize("staged", [False, True])
def test_run_rejects_changed_tracked_files(candidate: tuple[Path, dict[str, Any], Any], staged: bool) -> None:
    repo, _, check = candidate
    (repo / "input").write_text("different\n", encoding="utf-8")
    if staged:
        git(repo, "add", "input")
    with pytest.raises(SystemExit, match="tracked files differ"):
        check(123)
