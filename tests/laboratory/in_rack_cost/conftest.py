# SPDX-License-Identifier: GPL-3.0-or-later
"""The benchmark output is explicit, never silently written into the repository."""

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--laboratory-cost-out", required=True, help="raw benchmark JSON destination")
