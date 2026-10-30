# SPDX-License-Identifier: GPL-3.0-or-later
"""Deprecation warnings for the previously silent app and widget spellings."""

from typing import Any

import pytest

from rackpy import raw
from rackpy.raw import componentlibrary as lib
from rackpy.raw import ui, widget


def test_deprecated_aliases_and_component_types_warn() -> None:
    with pytest.warns(DeprecationWarning, match="ScrollBar"):
        assert ui.ScrollBar is ui.Scrollbar
    with pytest.warns(DeprecationWarning, match="SVGWidget"):
        assert widget.SVGWidget is widget.SvgWidget
    with pytest.warns(DeprecationWarning, match="LEDLightButton"):
        assert lib.LEDLightButton[lib.RedLight] is lib.VCVLightButton[lib.RedLight]
    with pytest.warns(DeprecationWarning, match="LEDSliderGreen"):
        slider = lib.LEDSliderGreen()
    slider.close()


def test_deprecated_scene_fields_warn_on_read_and_write() -> None:
    context = raw.context_get()
    assert context is not None and context.scene is not None
    scene = context.scene
    with pytest.warns(DeprecationWarning, match="mouse_pos"):
        pos = scene.mouse_pos
    with pytest.warns(DeprecationWarning, match="mouse_pos"):
        scene.mouse_pos = pos
    with pytest.warns(DeprecationWarning, match="touched_param"):
        param = scene.rack.touched_param
    with pytest.warns(DeprecationWarning, match="touched_param"):
        scene.rack.touched_param = param


def test_defining_legacy_draw_warns() -> None:
    with pytest.warns(DeprecationWarning, match="draw_vg"):

        class Legacy(widget.Widget):
            def draw_vg(self, graphics: Any) -> None:
                pass

    Legacy().close()
