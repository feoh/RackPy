# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22: the callback laboratory, inside actual Rack.

The suite runs at plugin start-up in the production build (no Rack context:
a Bench needs none), and again with a live context in the test-hooks build,
where it also compares a Bench with Rack's own engine.
"""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack"
UI_ONLY = ["test_engine_reference.py"]


def test_laboratory_in_production_host(make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    ignored = [arg for name in UI_ONLY for arg in ("--ignore", str(SUITE / name))]
    result = run_pytest_in_rack(make_bundle(), rack_host, tmp_path, SUITE, args=ignored)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()


@pytest.mark.gui
def test_laboratory_with_live_rack_context(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert any("test_a_bench_reproduces_racks_engine_frame_for_frame" in n for n in outcomes), result.summary()
    assert outcomes and set(outcomes.values()) == {"passed"}, result.summary()
