# SPDX-License-Identifier: GPL-3.0-or-later
"""Arguments for the Rack-hosted facade benchmark."""

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--rackpy-facade-cost", help="JSON path for facade timing samples")
