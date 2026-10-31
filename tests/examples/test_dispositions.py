# SPDX-License-Identifier: GPL-3.0-or-later
"""Every official sample of a completed example task has a tested disposition (upstream/examples.json).

RP-42 later requires one for every sample; until then the tasks that are done are listed here.
"""

import json
import re

import pytest

from tests.support import REPO

pytestmark = pytest.mark.unit
COMPLETED = {"RP-33", "RP-34", "RP-35", "RP-36", "RP-37"}
SAMPLES = json.loads((REPO / "upstream" / "examples.json").read_text(encoding="utf-8"))["samples"]


def test_completed_tasks_give_every_sample_a_disposition() -> None:
    missing = [s["id"] for s in SAMPLES if s["port_task"] in COMPLETED and "disposition" not in s]
    assert missing == []


@pytest.mark.parametrize("sample", [s for s in SAMPLES if "disposition" in s], ids=lambda s: s["id"])
def test_a_disposition_names_existing_files_and_tests(sample: dict[str, object]) -> None:
    disposition = sample["disposition"]
    assert isinstance(disposition, dict) and set(disposition) == {"how", "files", "tests"}
    assert disposition["how"] and disposition["files"] and disposition["tests"]
    for path in disposition["files"]:
        assert (REPO / path).exists(), path
    for node in disposition["tests"]:
        path, _, name = node.partition("::")
        source = (REPO / path).read_text(encoding="utf-8")
        assert re.search(rf"^def {re.escape(name)}\(", source, re.MULTILINE), node


# RP-36's end-to-end audit. RP-37 documented the shell commands and migration notes that RackPy
# does not adapt (dispositions above); RP-39 documents the packaging ones; samples about building
# Rack itself are out of scope.
LATER = {"RP-39"}
OFFICIAL_PAGES = ("tutorial-", "guide-", "helper-")


def test_every_tutorial_guide_and_template_sample_has_a_disposition() -> None:
    missing = [s["id"] for s in SAMPLES if s["id"].startswith(OFFICIAL_PAGES) and "disposition" not in s]
    assert missing == []


def test_every_other_sample_is_resolved_or_assigned() -> None:
    unresolved = [
        s["id"]
        for s in SAMPLES
        if "disposition" not in s
        and not (s["port_task"] in LATER and s["classification"] in {"shell", "config", "out-of-scope"})
        and not (s["port_task"] == "none" and s["classification"] == "out-of-scope" and s.get("reason"))
    ]
    assert unresolved == []
