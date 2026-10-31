# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the inventory-driven deprecated typedef tests in actual Rack."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, rack_platforms()]
SUITE = Path(__file__).resolve().parent / "in_rack"


def test_deprecated_aliases_in_live_rack(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    inventory = json.loads((SUITE.parents[2] / "upstream/api-surface.json").read_text())
    manifest = json.loads((SUITE.parents[2] / "bindings/manifest.json").read_text())
    recorded = {entry["id"] for entry in manifest["entries"]}
    count = sum(
        1
        for entry in inventory["entries"]
        if entry["kind"] == "alias"
        and (entry.get("deprecated") or entry.get("comment_deprecated"))
        and entry["id"] in recorded
    )
    aliases = {node: outcome for node, outcome in outcomes.items() if "test_deprecated_alias_warns" in node}
    total = sum(
        1
        for entry in inventory["entries"]
        if (entry.get("deprecated") or entry.get("comment_deprecated")) and entry["id"] in recorded
    )
    operations = {node: outcome for node, outcome in outcomes.items() if "test_deprecated_operation_warns" in node}
    assert count > 0 and len(aliases) == count and len(aliases) + len(operations) == total, result.summary()
    assert set(outcomes.values()) == {"passed"}, result.summary()
