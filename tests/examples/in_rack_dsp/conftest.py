# SPDX-License-Identifier: GPL-3.0-or-later
"""The in-Rack RP-36 suite's options: the C++ polyphony reference, computed outside Rack."""

import json
from pathlib import Path
from typing import Any

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--polyphony-reference", help="JSON from rackpy_polyphony_reference")


@pytest.fixture(scope="session")
def polyphony_reference(request: pytest.FixtureRequest) -> dict[str, Any]:
    path = request.config.getoption("--polyphony-reference")
    assert path, "the outer test passes --polyphony-reference"
    data: dict[str, Any] = json.loads(Path(path).read_text())
    return data
