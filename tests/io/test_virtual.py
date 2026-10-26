# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29C: Python-configured audio and MIDI drivers, in actual Rack."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
HERE = Path(__file__).resolve().parent


def test_virtual_devices_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, HERE / "in_rack_virtual", during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
