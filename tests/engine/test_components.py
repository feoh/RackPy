# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-13: run the engine component suite (tests/engine/in_rack) inside Rack.

Twice: on the production build at plugin start-up, where the tests that need a
Rack-owned module skip, and from the UI thread of windowed Rack on the
test-hooks build, where everything runs against a live engine. Both compare
with the compiled C++ reference (native/tests/components_reference.cpp).
"""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import InRackResult, linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack"
BORROWED = "test_components_borrowed.py::"


@pytest.fixture(scope="module")
def reference(dev_build: Path, tmp_path_factory: pytest.TempPathFactory) -> Path:
    out = tmp_path_factory.mktemp("reference") / "components_reference.json"
    with out.open("w") as f:
        subprocess.run([str(dev_build / "rackpy_components_reference")], stdout=f, check=True)
    return out


def test_suite_passes_inside_rack_on_the_production_build(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, reference: Path
) -> None:
    result = run_pytest_in_rack(make_bundle(), rack_host, tmp_path, SUITE, args=["--rackpy-reference", str(reference)])
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    skipped = {n for n, o in outcomes.items() if o == "skipped"}
    assert {n for n, o in outcomes.items() if o == "failed"} == set(), result.summary()
    assert skipped and all(BORROWED in n for n in skipped), skipped
    assert all(o == "passed" for n, o in outcomes.items() if BORROWED not in n)


def test_suite_passes_from_the_ui_thread_with_a_live_engine(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, reference: Path
) -> None:
    result: InRackResult = run_pytest_in_rack(
        make_hooks_bundle(), rack_host, tmp_path, SUITE, args=["--rackpy-reference", str(reference)], during_ui=True
    )
    assert result.exit == 0, result.summary()
    outcomes = result.outcomes()
    assert outcomes and set(outcomes.values()) == {"passed"}, result.summary()
    assert sum(BORROWED in n for n in outcomes) == 4
