# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the DSP suite that runs inside Rack (tests/dsp/test_basic.py starts it).

native/tests/dsp_reference.cpp writes one list of cases for all of RP-18; each
test file replays one section of it, chosen by the case's name.
"""

import json
from pathlib import Path
from typing import Any

import pytest

# Case-name prefixes of each section other than RP-18A's (which takes the rest).
SECTIONS = {
    "digital": ("boolean_trigger.", "schmitt.", "schmitt4.", "pulse.", "timer.", "timer4.", "clock_divider."),
    "filters": ("rc", "smoothers", "deprecated_smoothers", "iir", "biquad"),
}


def section(fn: str) -> str:
    return next((name for name, prefixes in SECTIONS.items() if fn.startswith(prefixes)), "basic")


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--rackpy-reference", help="JSON from the rackpy_dsp_reference program")
    parser.addoption("--rackpy-call-cost", help="where test_call_cost.py writes its measurements as JSON")


@pytest.fixture(scope="session")
def reference(request: pytest.FixtureRequest) -> list[dict[str, Any]]:
    path = request.config.getoption("--rackpy-reference")
    if not path:
        pytest.fail("--rackpy-reference was not given", pytrace=False)
    cases: list[dict[str, Any]] = json.loads(Path(path).read_text())["cases"]
    return cases


@pytest.fixture(scope="session")
def basic_cases(reference: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [c for c in reference if section(c["fn"]) == "basic"]


@pytest.fixture(scope="session")
def digital_cases(reference: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [c for c in reference if section(c["fn"]) == "digital"]


@pytest.fixture(scope="session")
def filter_cases(reference: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [c for c in reference if section(c["fn"]) == "filters"]
