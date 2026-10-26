# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-30C: rack::settings, Rack's shared mutex and weak pointers, in actual Rack."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
HERE = Path(__file__).resolve().parent


def test_settings_mutex_and_weak_pointers_in_rack(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, HERE / "in_rack_settings", during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
