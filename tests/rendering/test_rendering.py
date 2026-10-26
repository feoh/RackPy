# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-27: NanoVG drawing and window resources in actual Rack's live UI.

Rack renders with Mesa's software renderer under Xvfb. The pixel checks read
the framebuffers Rack rendered (premultiplied RGBA) with tolerances, so they
assume an OpenGL 2 renderer with NanoVG's usual antialiasing; no other GPU or
driver is tested.
"""

from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import linux_x64_only, run_pytest_in_rack

pytestmark = [pytest.mark.host, pytest.mark.gui, linux_x64_only()]
SUITE = Path(__file__).resolve().parent / "in_rack"


def test_rendering_in_live_rack(make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path) -> None:
    result = run_pytest_in_rack(make_hooks_bundle(), rack_host, tmp_path, SUITE, during_ui=True)
    assert result.exit == 0, result.summary()
    assert result.outcomes() and set(result.outcomes().values()) == {"passed"}, result.summary()
    # Python-owned resources held until Rack quit were released before Rack deleted its window.
    assert result.run.lines("window-resources-teardown-ok"), result.summary()
