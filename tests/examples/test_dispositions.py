# SPDX-License-Identifier: GPL-3.0-or-later
"""Every official sample has a tested disposition, or is out of scope with a reason (upstream/examples.json).

These tests check that the dispositions name files and tests that exist. Whether those tests passed on each
platform is RP-42's sample parity: bin/check_coverage.py samples, from each platform's test outcomes.
"""

import json
import re

import pytest

from tests.support import REPO

pytestmark = pytest.mark.unit
COMPLETED = {"RP-33", "RP-34", "RP-35", "RP-36", "RP-37", "RP-39"}
SAMPLES = json.loads((REPO / "upstream" / "examples.json").read_text(encoding="utf-8"))["samples"]


def test_completed_tasks_give_every_sample_a_disposition() -> None:
    missing = [s["id"] for s in SAMPLES if s["port_task"] in COMPLETED and "disposition" not in s]
    assert missing == []


@pytest.mark.parametrize("sample", [s for s in SAMPLES if "disposition" in s], ids=lambda s: s["id"])
def test_a_disposition_names_existing_files_and_tests(sample: dict[str, object]) -> None:
    disposition = sample["disposition"]
    assert isinstance(disposition, dict) and set(disposition) in (
        {"how", "files", "tests"},
        {"how", "files", "tests", "instrument_tests"},
    )
    assert disposition["how"] and disposition["files"] and disposition["tests"]
    for test, platforms in disposition.get("instrument_tests", {}).items():
        assert test in disposition["tests"]
        assert platforms == ["mac-x64", "mac-arm64", "win-x64"]
    for path in disposition["files"]:
        assert (REPO / path).exists(), path
    for node in disposition["tests"]:
        path, _, name = node.partition("::")
        source = (REPO / path).read_text(encoding="utf-8")
        assert re.search(rf"^def {re.escape(name)}\(", source, re.MULTILINE), node


# RP-36's end-to-end audit. RP-37 documented the shell commands and migration notes that RackPy
# does not adapt (dispositions above), and RP-39 the packaging ones; samples about building Rack
# itself are out of scope. No task is left to give the rest a disposition.
LATER: set[str] = set()
OFFICIAL_PAGES = ("tutorial-", "guide-", "helper-")


def test_audio_guard_skip_exceptions_are_exact_and_narrow() -> None:
    actual = {
        sample["id"]: sample["disposition"]["instrument_tests"]
        for sample in SAMPLES
        if "instrument_tests" in sample.get("disposition", {})
    }
    guard_platforms = ["mac-x64", "mac-arm64", "win-x64"]
    assert actual == {
        "guide-07": {"tests/dsp/test_runtime.py::test_the_kernel_path_makes_no_forbidden_calls": guard_platforms},
        "guide-13": {
            "tests/examples/test_dsp_expanders.py::test_the_example_kernels_make_no_audio_path_calls": guard_platforms
        },
        "guide-25": {
            "tests/examples/test_dsp_expanders.py::test_the_example_kernels_make_no_audio_path_calls": guard_platforms
        },
        "guide-30": {
            "tests/examples/test_dsp_expanders.py::test_the_example_kernels_make_no_audio_path_calls": guard_platforms
        },
    }


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
