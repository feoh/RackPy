# SPDX-License-Identifier: GPL-3.0-or-later
"""Fixtures for the engine component suite that runs inside Rack (tests/engine/test_components.py starts it)."""

import json
from pathlib import Path
from typing import Any

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--rackpy-reference", help="JSON from the rackpy_components_reference program")


@pytest.fixture(scope="session")
def reference(request: pytest.FixtureRequest) -> list[dict[str, Any]]:
    path = request.config.getoption("--rackpy-reference")
    if not path:
        pytest.fail("--rackpy-reference was not given", pytrace=False)
    data = json.loads(Path(path).read_text())
    cases: list[dict[str, Any]] = data["cases"]
    return cases


@pytest.fixture
def probe() -> Any:
    """A RuntimeProbe module Rack owns, added for one test and removed after it.

    Needs the test-hooks build and a live Rack context, i.e. the run that
    tests/engine/test_components.py starts from the UI thread.
    """
    try:
        from _rackpy_host import _testing
    except ImportError:
        pytest.skip("needs the test-hooks build")
    import _rackpy_host

    if _rackpy_host.bound_context_generation() is None:
        pytest.skip("needs a live Rack context (the UI-time run)")
    module = _testing.add_module()
    yield module
    if module.alive:
        _testing.remove_module(module)
