# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the RP-22 laboratory suite, run inside Rack only (tests/laboratory/test_laboratory.py starts it)."""

from collections.abc import Iterator

import pytest

from rackpy import laboratory

# Recorded as the suite is collected, before any test can enable the laboratory.
ENABLED_AT_START = laboratory.enabled()


@pytest.fixture
def enabled_at_start() -> bool:
    return ENABLED_AT_START


@pytest.fixture(autouse=True)
def lab() -> Iterator[None]:
    """Each test enables the laboratory, and leaves it disabled again."""
    laboratory.enable()
    try:
        yield
    finally:
        laboratory.disable()
