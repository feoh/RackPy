# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-12: run the family 1 suite (tests/values/in_rack) inside Rack, on the plugins RackPy builds.

The suite runs twice: on the test-hooks build, which can also take paths only
Rack can (a C++ rack::Exception, a GIL-free call into a Python override, an
engine-thread read), and on the production build, where those few tests skip
and everything else must pass. Both compare against the compiled C++
reference (native/tests/values_reference.cpp).
"""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import InRackResult, linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack"
HOOK_TESTS = {
    "test_common.py::test_rack_exceptions_arrive_as_rackpy_raw_exception",
    "test_quantity.py::test_an_override_that_raises_never_reaches_rack",
    "test_quantity.py::test_engine_thread_reads_never_touch_python",
    "test_quantity.py::test_plain_quantity_off_thread_falls_back_without_python",
}


@pytest.fixture(scope="module")
def reference(dev_build: Path, tmp_path_factory: pytest.TempPathFactory) -> Path:
    out = tmp_path_factory.mktemp("reference") / "values_reference.json"
    with out.open("w") as f:
        subprocess.run([str(dev_build / "rackpy_values_reference")], stdout=f, check=True)
    return out


def run(bundle: Path, rack: Path, user: Path, reference: Path) -> InRackResult:
    return run_pytest_in_rack(bundle, rack, user, SUITE, args=["--rackpy-reference", str(reference)])


@pytest.fixture(scope="module")
def with_hooks(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path_factory: pytest.TempPathFactory, reference: Path
) -> InRackResult:
    return run(make_hooks_bundle(), rack_host, tmp_path_factory.mktemp("in-rack-hooks"), reference)


@pytest.fixture(scope="module")
def production(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path_factory: pytest.TempPathFactory, reference: Path
) -> InRackResult:
    return run(make_bundle(), rack_host, tmp_path_factory.mktemp("in-rack-production"), reference)


def test_suite_passes_inside_rack_with_test_hooks(with_hooks: InRackResult) -> None:
    assert with_hooks.exit == 0, with_hooks.summary()
    outcomes = with_hooks.outcomes()
    assert outcomes and set(outcomes.values()) == {"passed"}, with_hooks.summary()
    assert len(outcomes) >= 40


def test_suite_passes_inside_rack_on_the_production_build(production: InRackResult) -> None:
    assert production.exit == 0, production.summary()
    skipped = {node.split("/")[-1] for node, outcome in production.outcomes().items() if outcome == "skipped"}
    failed = [node for node, outcome in production.outcomes().items() if outcome == "failed"]
    assert failed == [], production.summary()
    assert skipped == HOOK_TESTS, "only the tests that need the test-hooks build may skip"
