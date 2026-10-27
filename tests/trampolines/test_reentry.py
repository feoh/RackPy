# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared trampoline dispatch in actual Rack, with a live UI context."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]


def test_trampoline_reentry_in_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(
        make_hooks_bundle(), rack_host, tmp_path, Path(__file__).parent / "in_rack", during_ui=True
    )
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
