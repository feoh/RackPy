# SPDX-License-Identifier: GPL-3.0-or-later
"""CI runs the documentation tests in shards (.github/workflows/ci.yml, the docs job): each test must run in one."""

import re
import subprocess
import sys

import pytest

from tests.support import REPO

pytestmark = pytest.mark.unit

SELECTION = "docs and not host and not sanitizer and not stress"


def collect(*extra: str) -> set[str]:
    proc = subprocess.run(
        [sys.executable, "-m", "pytest", "-m", SELECTION, "--collect-only", "-q", "-p", "no:cacheprovider", *extra],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=True,
    )
    return {line for line in proc.stdout.splitlines() if "::" in line}


def shard_selections() -> dict[str, str]:
    """The docs job's matrix, read from the workflow text (the project has no YAML dependency)."""
    text = (REPO / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    job = text.split("\n  docs:\n", 1)[1].split("\n  docs-links:\n", 1)[0]
    return dict(re.findall(r'- shard: (\S+)\n\s+selection: "([^"\n]*)"', job))


def test_unreliable_external_link_check_runs_only_weekly() -> None:
    text = (REPO / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    job = text.split("\n  docs-links:\n", 1)[1].split("\n  native:\n", 1)[0]
    assert "if: github.event_name == 'schedule'" in job
    assert "workflow_dispatch" not in job


def test_planning_job_does_not_poison_the_shared_uv_dependency_cache() -> None:
    text = (REPO / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    planning = text.split("\n  planning:\n", 1)[1].split("\n  python:\n", 1)[0]
    assert "cache-python: true" in planning
    assert "cache-suffix: planning" in planning

    python = text.split("\n  python:\n", 1)[1].split("\n  docs:\n", 1)[0]
    assert "cache-python: true" in python
    assert "cache-suffix: packages-v1" in python


def test_every_docs_test_runs_in_exactly_one_ci_shard() -> None:
    selections = shard_selections()
    assert len(selections) >= 2, selections
    shards = {name: collect("-k", expr) for name, expr in selections.items() if expr}
    everything = collect()
    assert everything, "no documentation tests collected"
    covered = set().union(*shards.values())
    assert covered == everything, (
        f"in no shard: {sorted(everything - covered)}; unknown: {sorted(covered - everything)}"
    )
    assert sum(map(len, shards.values())) == len(covered), "a test is in more than one shard"
