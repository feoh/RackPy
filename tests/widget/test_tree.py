# SPDX-License-Identifier: GPL-3.0-or-later
"""Detached tree tests run inside actual Rack, not a standalone fake host."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack"


def test_widget_startup_refusal(make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_bundle(), rack_host, tmp_path, SUITE, args=["-k", "startup_only"])
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()


@pytest.mark.gui
def test_widget_live_ui(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(
        make_hooks_bundle(), rack_host, tmp_path, SUITE, args=["-k", "not startup_only"], during_ui=True
    )
    assert result.run.lines("detached-widget-cleanup-ok"), result.summary()
    assert not result.run.lines("UI cleanup failed"), result.summary()
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
