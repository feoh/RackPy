# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-31J: native audio-port and MIDI-input I/O for kernels and graphs, in actual Rack (live UI, test hooks)."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack_io_paths"


def test_io_paths_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True, timeout=300)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
    ran = {name.split("::", 1)[0].rsplit("/", 1)[-1] for name in result.outcomes()}
    assert ran == {p.name for p in SUITE.glob("test_*.py")}, f"every in-Rack test file reports: {sorted(ran)}"
