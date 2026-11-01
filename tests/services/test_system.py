# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-30B: rack::system and rack::network, in actual Rack (loopback HTTP only, temporary directories)."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import WINDOWS, rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, rack_platforms()]
HERE = Path(__file__).resolve().parent


def test_system_and_network_in_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, HERE / "in_rack_system", during_ui=True)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    # On Windows Rack's runProcessDetached would ask UAC to elevate; that one test is skipped there.
    expected_skips = {"test_system.py::test_run_process_detached_is_windows_only"} if WINDOWS else set()
    assert outcomes and {t for t, o in outcomes.items() if o != "passed"} == expected_skips, result.summary()
    assert all(outcomes[t] == "skipped" for t in expected_skips), result.summary()
