# SPDX-License-Identifier: GPL-3.0-or-later
"""The in-Rack oscillator suite's options: the C++ reference's output, computed outside Rack."""

import json
import sys
from pathlib import Path
from typing import Any

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))  # the repository, for tests.examples.signal


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--oscillator-reference", help="JSON from rackpy_oscillator_reference, by 'rate/stream'")


@pytest.fixture(scope="session")
def reference(request: pytest.FixtureRequest) -> dict[str, Any]:
    path = request.config.getoption("--oscillator-reference")
    assert path, "the outer test passes --oscillator-reference"
    data: dict[str, Any] = json.loads(Path(path).read_text())
    return data
