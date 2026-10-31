# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-38: tests/hardening/in_rack_stress in Rack's live UI, with the audio guard preloaded (opt-in: -m stress)."""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, pytest.mark.stress, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack_stress"
# The entry module adds the stress models while Rack loads the plugin.
SETUP = f"import sys\nsys.path.insert(0, {str(SUITE)!r})\nimport stress_models  # noqa: F401\n"
MODELS: list[dict[str, object]] = [{"slug": "Churn", "name": "Churn", "description": "RP-38 stress model"}]
HOSTILE_LINE = "RP-38 hostile callback"


def test_stress_in_live_rack(
    make_hooks_bundle: Callable[[], Path], hooks_build: Path, rack_host: Path, tmp_path: Path
) -> None:
    result = run_pytest_in_rack(
        make_hooks_bundle(),
        rack_host,
        tmp_path,
        SUITE,
        args=["-s"],
        during_ui=True,
        timeout=1800,  # a deadlock fails here, as a timeout
        entry_setup=SETUP,
        extra_models=MODELS,
        env={"LD_PRELOAD": str(hooks_build / "audio_guard.so")},
    )
    assert result.exit == 0, result.summary()
    print("".join(line + "\n" for line in result.output.splitlines() if (": " in line and "{" in line) or "/s" in line))
    outcomes = result.outcomes()
    assert len(outcomes) == 7 and set(outcomes.values()) == {"passed"}, result.summary()
    assert "audio guard inside RackPy's process(): {" in result.output  # the guard was preloaded and saw zero
    # Bounded diagnostics: 5,000 raises, and every hostile phase, leave a few dozen lines in Rack's log.
    log = result.run.log
    hostile = [line for line in log.splitlines() if HOSTILE_LINE in line]
    assert 20 <= len(hostile) <= 60, len(hostile)
    assert "from now on its errors are logged at most once a second" in log
    assert not result.run.lines("Fatal signal")
    assert log.rstrip().endswith("END") or "Destroying logger" in log  # Rack quit cleanly around the live modules
