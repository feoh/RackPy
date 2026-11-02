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
