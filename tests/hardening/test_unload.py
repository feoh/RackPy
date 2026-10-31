# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: tests/hardening/in_rack_unload, in Rack's live UI (test-hooks build)."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack_unload"


def test_files_removed_under_a_running_plugin(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert len(outcomes) == 1 and set(outcomes.values()) == {"passed"}, result.summary()
    assert not result.run.lines("Fatal signal") and result.run.lines("Destroying logger")
