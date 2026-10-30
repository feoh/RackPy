# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the facade timing probe in the actual Linux Rack host."""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
SUITE = Path(__file__).parent / "in_rack_benchmark"


def test_midi_facade_cost(make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    output = tmp_path / "midi-facade-cost.json"
    result = run_pytest_in_rack(
        make_bundle(), rack_host, tmp_path / "user", SUITE, args=["--rackpy-facade-cost", str(output)]
    )
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
    values = json.loads(output.read_text())
    assert len(values["raw_ns_per_message"]) == len(values["facade_ns_per_message"]) == 7
