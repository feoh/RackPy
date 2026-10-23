# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14A: run module construction tests in actual Rack, not just libRack."""

import subprocess
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "module" / "in_rack"


@pytest.fixture(scope="module")
def module_reference(dev_build: Path, tmp_path_factory: pytest.TempPathFactory) -> Path:
    output = tmp_path_factory.mktemp("module-reference") / "module.json"
    with output.open("w") as f:
        subprocess.run([str(dev_build / "rackpy_module_reference")], stdout=f, check=True)
    return output


def test_modules_in_production_host(
    make_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, module_reference: Path
) -> None:
    # Engine ownership needs the test hooks and a live engine: the UI run covers it.
    result = run_pytest_in_rack(
        make_bundle(),
        rack_host,
        tmp_path,
        SUITE,
        args=[
            "--rackpy-reference",
            str(module_reference),
            "--ignore",
            str(SUITE / "test_engine_ownership.py"),
            "--ignore",
            str(SUITE / "test_engine_lifecycle.py"),
            "--ignore",
            str(SUITE / "test_engine_bypass.py"),
            "--ignore",
            str(SUITE / "test_engine_save_reload.py"),
        ],
    )
    assert result.exit == 0, result.summary()
    skipped = {n for n, o in result.outcomes().items() if o == "skipped"}
    assert len(skipped) == 1 and next(iter(skipped)).endswith("::test_bypass_routes_match_native_reference")
    assert set(result.outcomes().values()) == {"passed", "skipped"}, result.summary()


@pytest.mark.gui
def test_modules_with_live_rack_context(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path, module_reference: Path
) -> None:
    result = run_pytest_in_rack(
        make_hooks_bundle(),
        rack_host,
        tmp_path,
        SUITE,
        args=["--rackpy-reference", str(module_reference)],
        during_ui=True,
    )
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
