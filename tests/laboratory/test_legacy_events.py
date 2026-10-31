# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the direct legacy event and override tests in a live Rack context."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, rack_platforms()]


def test_legacy_events_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    suite = Path(__file__).resolve().parent / "in_rack_legacy"
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, suite, during_ui=True)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert len(outcomes) == 6, result.summary()
    assert all(outcome == "passed" for outcome in outcomes.values()), result.summary()
