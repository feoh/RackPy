# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29C: Python-configured audio and MIDI drivers, in actual Rack."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import rack_platforms, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, rack_platforms()]
HERE = Path(__file__).resolve().parent


def test_virtual_devices_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, HERE / "in_rack_virtual", during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
    log = result.run.log  # test_rack_deletes_python_drivers_as_it_quits: Rack deleted both drivers, then destroy()
    deleted = log.index("RackPy: rp31e audio driver deleted: Rack deleted this audio driver (it quit)")
    assert log.index("Destroying MIDI") < log.index("Destroying audio") < log.index("Destroying plugins") < deleted
    assert "RackPy: rp31e midi driver deleted: Rack deleted this MIDI driver (it quit)" in log
    assert "RackPy: rp31e audio device alive: False" in log and "still alive" not in log
