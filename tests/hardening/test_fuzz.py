# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: tests/hardening/in_rack_fuzz, the boundary fuzzers, in Rack's live UI (test-hooks build)."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack_fuzz"


def test_the_boundaries_survive_fuzzing(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(
        make_hooks_bundle(), rack_host, tmp_path, SUITE, args=["-s"], during_ui=True, timeout=900
    )
    assert result.exit == 0, result.summary()
    print("".join(line + "\n" for line in result.output.splitlines() if ":" in line and "refused" in line), end="")
    outcomes = result.outcomes()
    assert len(outcomes) == 6 and set(outcomes.values()) == {"passed"}, result.summary()
