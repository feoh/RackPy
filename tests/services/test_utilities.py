# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-30A: rack::string, rack::random and rack::logger, in actual Rack."""

import re
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
HERE = Path(__file__).resolve().parent


def test_utilities_in_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, HERE / "in_rack_utilities", during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
    log = (tmp_path / "log.txt").read_text()
    assert "info rackpy_test.py:12 test_logging] rackpy-log marker 5" in log  # Rack's log, with the given location
    assert "test_utilities.py:" in log and "rackpy-macro warned" in log  # the macro names its Python caller
    for line, (level, label) in enumerate(
        (("DEBUG_LEVEL", "debug"), ("INFO_LEVEL", "info"), ("WARN_LEVEL", "warn"), ("FATAL_LEVEL", "fatal")), start=21
    ):
        assert f" {label} rp31e_levels.py:{line} levels] rp31e-level {level}\n" in log  # Rack's label for the level
        macro = level.removesuffix("_LEVEL")
        assert re.search(rf" {label} \S*test_utilities\.py:\d+ test_every_log_level\] rp31e-macro {macro}\n", log)
