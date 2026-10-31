# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-27B: framebuffers and Rack's self-lit layer over Rack's real frames (Xvfb, Mesa software OpenGL).

RackWidget draws the modules, dims the rack by Rack's room brightness, then
draws layer 1, the lights, undimmed. A Python widget in a module widget is
drawn on both layers; its pixels are read from the frames Rack drew. That
assumes Mesa's software GLX, which keeps the back buffer after a swap.
"""

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from tests.support import PROBE_MODULE, rack_platforms, run_rack_windowed, stage_plugin, write_patch

pytestmark = [pytest.mark.host, pytest.mark.gui, rack_platforms()]
HERE = Path(__file__).resolve().parent


def near(actual: list[int], expected: tuple[int, int, int], tolerance: int = 8) -> bool:
    return all(abs(a - e) <= tolerance for a, e in zip(actual[:3], expected, strict=True))


def test_framebuffers_and_the_self_lit_layer_in_real_frames(
    make_hooks_bundle: Callable[[], Path], rack_host: Path, tmp_path: Path
) -> None:
    result_path = tmp_path / "result.json"
    plugin = stage_plugin(
        make_hooks_bundle(),
        tmp_path,
        entry="rp27_frames",
        files={"pythonpath/rp27_frames.py": (HERE / "entries" / "rp27_frames.py").read_text()},
    )
    (plugin / "pythonpath" / "rp27_config.json").write_text(json.dumps({"result": str(result_path)}))
    write_patch(tmp_path, [PROBE_MODULE])
    run = run_rack_windowed(rack_host, tmp_path)
    assert run.returncode == 0 and not run.lines("Fatal signal"), run.tail()
    result = json.loads(result_path.read_text())
    assert "error" not in result, result["error"]

    # A FramebufferWidget renders its children only when dirty.
    assert result["first_render_frames"] is not None
    assert result["clean"] == [1, False]  # five more frames, no render
    assert result["dirty_render_frames"] is not None and result["after_dirty"] == 1
    assert result["subpixel_render_frames"] is not None  # a new subpixel offset re-renders...
    assert result["subpixel_ignored"] == 0  # ...unless dirty_on_subpixel_change is off
    assert result["bypassed_draws"] == 5  # bypassed: drawn directly, every frame
    assert near(result["framebuffer_pixel"], (0, 255, 0)), result  # Rack composited the framebuffer's image

    # Rack's self-lit layer: RackWidget dims the panel layer by the room brightness, then draws layer 1.
    # Compared with the undimmed panel, because anything Rack draws above the rack (its first-launch
    # tip window, for one) darkens both alike.
    assert result["layers"] == [0, 1]
    full = result["undimmed_panel_pixel"][0]
    assert full > 100 and result["undimmed_panel_pixel"][1:3] == [0, 0], result
    assert result["panel_pixel"][0] / full == pytest.approx(0.25, abs=0.03), result
    assert result["half_panel_pixel"][0] / full == pytest.approx(0.5, abs=0.03), result
    assert abs(result["lit_pixel"][0] - full) <= 4 and result["lit_pixel"][1:3] == [0, 0], result  # not dimmed
