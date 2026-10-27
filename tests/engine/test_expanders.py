# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-23: Rack's module expanders and the graph exchange (RP-23B), inside actual Rack (live UI, test hooks)."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "expanders" / "in_rack"


def test_expanders_in_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert outcomes and set(outcomes.values()) == {"passed"}, result.summary()
    assert len(outcomes) >= 20  # 7 RP-23A tests, 9 rate/thread pairs and 2 more RP-23B tests, 2 RP-23C tests
