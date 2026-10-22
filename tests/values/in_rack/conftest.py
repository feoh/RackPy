# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the family 1 suite that runs inside Rack (tests/values/test_values_in_rack.py starts it)."""

import json
from pathlib import Path
from typing import Any

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--rackpy-reference", help="JSON from the rackpy_values_reference program")


@pytest.fixture(scope="session")
def reference(request: pytest.FixtureRequest) -> list[dict[str, Any]]:
    path = request.config.getoption("--rackpy-reference")
    if not path:
        pytest.fail("--rackpy-reference was not given", pytrace=False)
    data = json.loads(Path(path).read_text())
    cases: list[dict[str, Any]] = data["cases"]
    return cases
