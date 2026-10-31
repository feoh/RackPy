# SPDX-License-Identifier: GPL-3.0-or-later
"""The in-Rack RP-34 suite's options: where the examples are, for the laboratory script."""

from pathlib import Path

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--examples", help="the repository's examples directory")


@pytest.fixture(scope="session")
def examples(request: pytest.FixtureRequest) -> Path:
    path = request.config.getoption("--examples")
    assert path, "the outer test passes --examples"
    return Path(path)
