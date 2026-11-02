# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared synthetic checker tests are not evidence of bindings on any platform."""

import binding_manifest as bm
import pytest

pytestmark = pytest.mark.unit

SHARED_SUITES = (
    "tests/coverage/test_mutations.py",
    "tests/coverage/test_release_mutations.py",
)


def test_shared_checker_suites_are_not_cited_as_platform_evidence() -> None:
    manifest = bm.load_json(bm.MANIFEST)
    samples = bm.load_json(bm.REPO / "upstream/examples.json")
    citations = [test for record in manifest["entries"] for test in record.get("tests", [])]
    citations += [test for sample in samples["samples"] for test in (sample.get("disposition") or {}).get("tests", [])]
    cited_shared = [
        test for test in citations if any(test == suite or test.startswith(suite + "::") for suite in SHARED_SUITES)
    ]
    assert not cited_shared
