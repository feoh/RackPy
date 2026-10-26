# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the DSP suite that runs inside Rack (tests/dsp/test_basic.py starts it)."""

import json
from pathlib import Path
from typing import Any

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--rackpy-reference", help="JSON from the rackpy_dsp_reference program")


@pytest.fixture(scope="session")
def reference(request: pytest.FixtureRequest) -> list[dict[str, Any]]:
    path = request.config.getoption("--rackpy-reference")
    if not path:
        pytest.fail("--rackpy-reference was not given", pytrace=False)
    cases: list[dict[str, Any]] = json.loads(Path(path).read_text())["cases"]
    return cases
