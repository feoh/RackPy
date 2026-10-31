# SPDX-License-Identifier: GPL-3.0-or-later
"""Every official sample of a completed example task has a tested disposition (upstream/examples.json).

RP-42 later requires one for every sample; until then the tasks that are done are listed here.
"""

import json
import re

import pytest

from tests.support import REPO

pytestmark = pytest.mark.unit
COMPLETED = {"RP-33", "RP-34", "RP-35"}
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
