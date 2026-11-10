# SPDX-License-Identifier: GPL-3.0-or-later
"""bin/ci-failures (RP-56): what it reads from a job log, and how it sorts failed tests by platform."""

import importlib.machinery
import importlib.util
import types

import pytest

from tests.support import BIN, REPO

pytestmark = pytest.mark.unit

LOG = """\
\ufeff2026-11-05T10:00:00.0000000Z ##[group]Run uv run --frozen pytest
2026-11-05T10:00:01.0000000Z tests/dsp/test_runtime.py::test_kernels_in_rack \x1b[31mFAILED\x1b[0m [ 50%]
2026-11-05T10:00:02.0000000Z E       AssertionError: test_reset (call)
2026-11-05T10:00:03.0000000Z =========================== short test summary info ===========================
2026-11-05T10:00:04.0000000Z FAILED tests/dsp/test_runtime.py::test_kernels_in_rack - AssertionError: test_reset (call)
2026-11-05T10:00:05.0000000Z ERROR tests/app/test_app.py::test_app_widgets_in_live_rack
2026-11-05T10:00:06.0000000Z ##[error]Process completed with exit code 1.
"""

HOOKS = """\
2026-11-07T03:24:50.0000000Z trim trailing whitespace.................................................Failed
2026-11-07T03:24:50.0000000Z ruff check...............................................................Passed
"""


@pytest.fixture(scope="module")
def tool() -> types.ModuleType:
    loader = importlib.machinery.SourceFileLoader("ci_failures", str(BIN / "ci-failures"))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


def test_a_job_log_gives_its_failed_tests_and_otherwise_the_lines_that_look_like_the_cause(
    tool: types.ModuleType,
) -> None:
    tests, lines = tool.read_log(tool.clean(LOG))
    assert tests == {
        "tests/dsp/test_runtime.py::test_kernels_in_rack": "AssertionError: test_reset (call)",
        "tests/app/test_app.py::test_app_widgets_in_live_rack": "",
    }
    assert lines == ["E       AssertionError: test_reset (call)"]
    assert tool.read_log(tool.clean(HOOKS)) == (
        {},
        ["trim trailing whitespace.................................................Failed"],
    )


def test_every_ci_job_name_maps_to_its_platform(tool: types.ModuleType) -> None:
    workflow = (REPO / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    assert tool.platform_of("Windows x64 dsp suite, native build, CTest, unit and Rack host tests") == "win-x64"
    assert tool.platform_of("Windows x64 release candidate, installed and run in fresh Rack user folders") == "win-x64"
    assert tool.platform_of("macOS mac-arm64 app suite, native build, CTest, unit and Rack host tests") == "mac-arm64"
    assert (
        tool.platform_of("macOS mac-x64 release candidate, installed and run in fresh Rack user folders") == "mac-x64"
    )
    assert tool.platform_of("SDK inventory on win-x64") == "win-x64"
    assert tool.platform_of("Rack host smoke tests (headless and Xvfb, no audio device)") == "lin-x64"
    # The names above are the workflow's: a renamed job must not silently become "Linux".
    for name in (
        "name: Windows x64 ",
        "name: macOS ${{ matrix.platform }} ",
        "name: SDK inventory on ${{ matrix.platform }}",
    ):
        assert name in workflow, name


def test_linux_or_several_platforms_is_a_bug_and_one_other_platform_may_be_intermittent(
    tool: types.ModuleType,
) -> None:
    def failed(platform: str, *tests: str) -> object:
        return tool.Failure("job", 1, platform, [], dict.fromkeys(tests, ""))

    kinds = tool.classify(
        [failed("lin-x64", "t::linux"), failed("mac-x64", "t::both", "t::mac"), failed("win-x64", "t::both")]
    )
    assert kinds == {
        "several": {"t::both": {"mac-x64", "win-x64"}, "t::linux": {"lin-x64"}},
        "single": {"t::mac": {"mac-x64"}},
    }
