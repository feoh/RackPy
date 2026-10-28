# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-28C: rack::componentlibrary in Rack's live UI: Rack's own components and its templates as specializations.

The components load Rack's installed artwork; nothing here copies it.
"""

import math
from collections.abc import Iterator
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw import nanovg as nvg
from rackpy.raw import widget as w
from rackpy.raw.engine import Module, Port
from rackpy.raw.math import Rect, Vec
from rackpy.raw.window import mm2px

KNOBS = [
    "RoundBlackKnob", "RoundSmallBlackKnob", "RoundLargeBlackKnob", "RoundBigBlackKnob", "RoundHugeBlackKnob",
    "RoundBlackSnapKnob", "Davies1900hWhiteKnob", "Davies1900hBlackKnob", "Davies1900hRedKnob",
    "Davies1900hLargeWhiteKnob", "Davies1900hLargeBlackKnob", "Davies1900hLargeRedKnob", "Rogan6PSWhite",
    "Rogan5PSGray", "Rogan3PSBlue", "Rogan3PSRed", "Rogan3PSGreen", "Rogan3PSWhite", "Rogan3PBlue", "Rogan3PRed",
    "Rogan3PGreen", "Rogan3PWhite", "Rogan2SGray", "Rogan2PSBlue", "Rogan2PSRed", "Rogan2PSGreen", "Rogan2PSWhite",
    "Rogan2PBlue", "Rogan2PRed", "Rogan2PGreen", "Rogan2PWhite", "Rogan1PSBlue", "Rogan1PSRed", "Rogan1PSGreen",
    "Rogan1PSWhite", "Rogan1PBlue", "Rogan1PRed", "Rogan1PGreen", "Rogan1PWhite", "SynthTechAlco", "Trimpot",
    "BefacoBigKnob", "BefacoTinyKnob",
]  # fmt: skip
SWITCHES = [
    "NKK", "CKSS", "CKSSThree", "CKSSThreeHorizontal", "CKD6", "TL1105", "VCVButton", "VCVLatch", "BefacoSwitch",
    "BefacoPush", "VCVBezel", "VCVBezelLatch", "PB61303",
]  # fmt: skip
PORTS = ["PJ301MPort", "DarkPJ301MPort", "ThemedPJ301MPort", "PJ3410Port", "CL1362Port"]


def configure(m: Module) -> None:
    m.config(2, 1, 1, 6)
    m.config_param(0, 0, 1, 0, "Button")
    m.config_param(1, 0, 10, 5, "Level")


@pytest.fixture
def module() -> Iterator[Module]:
    m = Module.create(configure)
    HOOKS.engine_adopt(m)
    yield m
    if m.alive:
        HOOKS.engine_delete(m, None)


def render(widget: w.Widget, size: Vec) -> tuple[int, int, bytes]:
    frame = w.FramebufferWidget()
    frame.box = Rect(Vec(), size)
    frame.add_child(widget)
    try:
        frame.render()
        return HOOKS.framebuffer_pixels(frame)
    finally:
        frame.close()


def pixel(image: tuple[int, int, bytes], x: int, y: int) -> tuple[int, ...]:
    width, _, data = image
    i = (y * width + x) * 4
    return tuple(data[i : i + 4])


def test_racks_components_load_racks_artwork() -> None:
    made: list[w.Widget] = []
    try:
        for name in KNOBS:
            knob = getattr(cl, name)()
            made.append(knob)
            assert isinstance(knob, app.SvgKnob) and knob.box.size.x > 0, name
            assert knob.sw.svg is not None and knob.min_angle < 0 < knob.max_angle, name
            if hasattr(knob, "bg"):
                assert isinstance(knob.bg, w.HostSvgWidget) and knob.bg.svg is not None, name
        assert cl.RoundBlackSnapKnob().snap and isinstance(cl.Rogan1PRed().fg, w.HostSvgWidget)
        assert cl.RoundBlackKnob().min_angle == pytest.approx(-0.83 * math.pi)
        for name in ("BefacoSlidePot", "VCVSlider"):
            slider = getattr(cl, name)()
            made.append(slider)
            assert isinstance(slider, app.SvgSlider) and slider.box.size.y > slider.box.size.x
        for name in SWITCHES:
            switch = getattr(cl, name)()
            made.append(switch)
            assert isinstance(switch, app.SvgSwitch) and switch.frames, name
        assert len(cl.NKK().frames) == 3 and cl.CKD6().momentary and cl.CKSS().shadow.opacity == 0
        latch = cl.VCVLatch()
        assert latch.latch and not latch.momentary and isinstance(latch, cl.VCVButton)
        for name in PORTS:
            port = getattr(cl, name)()
            made.append(port)
            assert isinstance(port, app.SvgPort) and port.box.size.x > 0, name
        themed = cl.ThemedPJ301MPort()
        assert themed.light_svg is not None and themed.dark_svg is not None
        for name in ("ScrewSilver", "ScrewBlack", "ThemedScrew"):
            screw = getattr(cl, name)()
            made.append(screw)
            assert isinstance(screw, app.SvgScrew) and screw.box.size.x > 0
        assert cl.LEDSlider is cl.VCVSlider and cl.LEDButton is cl.VCVButton and cl.LEDBezel is cl.VCVBezel
        assert nvg.rgb(0xED, 0x2C, 0x24) == cl.SCHEME_RED and nvg.rgb(0x17, 0x17, 0x17) == cl.SCHEME_DARK_GRAY

        class Mine(cl.RoundBlackKnob):
            pass

        assert Mine().box.size == cl.RoundBlackKnob().box.size  # Python subclasses run Rack's constructor
    finally:
        for widget in made:
            widget.close()


def test_scheme_colors_match_the_pinned_sdk() -> None:
    # componentlibrary.hpp defines distinct RGBA values, not interchangeable aliases.
    expected = {
        "BLACK_TRANSPARENT": (0x00, 0x00, 0x00, 0x00),
        "BLACK": (0x00, 0x00, 0x00, 0xFF),
        "WHITE": (0xFF, 0xFF, 0xFF, 0xFF),
        "RED": (0xED, 0x2C, 0x24, 0xFF),
        "ORANGE": (0xF2, 0xB1, 0x20, 0xFF),
        "YELLOW": (0xFF, 0xD7, 0x14, 0xFF),
        "GREEN": (0x90, 0xC7, 0x3E, 0xFF),
        "CYAN": (0x22, 0xE6, 0xEF, 0xFF),
        "BLUE": (0x29, 0xB2, 0xEF, 0xFF),
        "PURPLE": (0xD5, 0x2B, 0xED, 0xFF),
        "LIGHT_GRAY": (0xE6, 0xE6, 0xE6, 0xFF),
        "DARK_GRAY": (0x17, 0x17, 0x17, 0xFF),
    }
    for name, rgba in expected.items():
        assert getattr(cl, f"SCHEME_{name}") == nvg.rgba(*rgba), name


def test_light_templates_are_specializations() -> None:
    red = cl.RedLight()
    assert red.base_colors == [cl.SCHEME_RED] and red.bg_color == nvg.rgba(0x33, 0x33, 0x33, 0xFF)
    assert cl.TRedLight[cl.GrayModuleLightWidget] is cl.RedLight and cl.TRedLight.default() is cl.RedLight
    assert issubclass(cl.RedLight, cl.GrayModuleLightWidget) and issubclass(
        cl.GrayModuleLightWidget, app.ModuleLightWidget
    )
    assert cl.GreenRedLight().base_colors == [cl.SCHEME_GREEN, cl.SCHEME_RED]
    assert len(cl.RedGreenBlueLight().base_colors) == 3
    medium: type[Any] = cl.MediumLight[cl.RedLight]
    assert medium is cl.MediumLight[cl.RedLight] and medium.__name__ == "MediumLight[RedLight]"
    lamp = medium()
    assert lamp.sw.svg is not None and lamp.box.size.x > 0  # Rack's MediumLight.svg
    assert isinstance(lamp, cl.RedLight)
    assert cl.LargeLight[cl.GreenLight]().box.size.x > lamp.box.size.x > cl.TinyLight[cl.BlueLight]().box.size.x
    assert cl.SmallSimpleLight[cl.YellowLight]().box.size == mm2px(Vec(2, 2))
    assert cl.TinySimpleLight[cl.BlueLight]().box.size == mm2px(Vec(1, 1))
    assert cl.LargeSimpleLight[cl.WhiteLight]().box.size == mm2px(Vec(5, 5))
    small = cl.SmallLight[cl.RedLight]()
    assert small.sw.svg is not None and small.box.size.x < lamp.box.size.x
    assert cl.BlueLight().base_colors == [cl.SCHEME_BLUE]
    assert cl.YellowLight().base_colors == [cl.SCHEME_YELLOW]
    assert cl.WhiteLight().base_colors == [cl.SCHEME_WHITE]
    assert cl.SvgLight is cl.TSvgLight.default()
    assert cl.MediumSimpleLight.default()().box.size == mm2px(Vec(3, 3))
    assert cl.PB61303Light[cl.WhiteLight]().box.size == mm2px(Vec(9, 9))
    bezel_light = cl.VCVBezelLight[cl.WhiteLight]()
    assert bezel_light.box.size == Vec(17.545, 17.545) and bezel_light.bg_color.a == 0
    # Rack's alias templates give the aliased specializations themselves.
    assert cl.LEDBezelLight[cl.WhiteLight] is cl.VCVBezelLight[cl.WhiteLight]
    assert cl.LEDLightBezel[cl.RedLight] is cl.VCVLightBezel[cl.RedLight]
    assert cl.LEDSliderLight[cl.BlueLight] is cl.VCVSliderLight[cl.BlueLight]
    assert cl.LEDLightSlider[cl.GreenLight] is cl.VCVLightSlider[cl.GreenLight]
    assert cl.VCVLightButton[cl.RedLight] is cl.LightButton[cl.VCVButton, cl.RedLight] is cl.LEDLightButton[cl.RedLight]
    assert isinstance(lamp, cl.MediumLight) and isinstance(lamp, cl.TSvgLight)  # every specialization is its template's
    with pytest.raises(TypeError, match="class template"):
        cl.MediumLight()
    for bad in ((), "x", (cl.RedLight, cl.RedLight)):
        with pytest.raises(TypeError):
            cl.MediumLight[bad]  # type: ignore[index]


class Rack(w.Widget):
    """Composes layers as Rack's RackWidget does: layer 0, then the self-lit layer 1."""

    def draw(self, args: Any) -> None:
        super().draw(args)
        for child in self.children:
            self.draw_child(child, args, 1)


def test_rectangle_lights_draw_with_racks_code() -> None:
    light = cl.RectangleLight[cl.GreenLight]()
    light.box = Rect(Vec(), Vec(10, 6))
    light.step()  # no module: Rack lights every colour fully
    background = render(light, Vec(10, 6))  # a framebuffer renders layer 0: the background
    assert pixel(background, 5, 3) == (0x33, 0x33, 0x33, 0xFF)
    holder = Rack()
    holder.box = Rect(Vec(), Vec(10, 6))
    light = cl.RectangleLight[cl.GreenLight]()
    light.box = Rect(Vec(), Vec(10, 6))
    light.step()
    holder.add_child(light)
    # Layer 1: Rack's green fills the rectangle, and Rack's own halo (LightWidget::drawHalo, which
    # RectangleLight keeps) brightens it a little.
    r, g, b, a = pixel(holder_image := render(holder, Vec(10, 6)), 5, 3)
    assert g >= 0xC7 - 4 and r >= 0x90 - 4 and b >= 0x3E - 4 and g > r > b and a == 255, pixel(holder_image, 5, 3)


def test_coloured_led_sliders_have_their_own_light() -> None:
    for slider_cls, light_cls, color in (
        (cl.LEDSliderRed, cl.RedLight, cl.SCHEME_RED),
        (cl.LEDSliderYellow, cl.YellowLight, cl.SCHEME_YELLOW),
        (cl.LEDSliderGreen, cl.GreenLight, cl.SCHEME_GREEN),
        (cl.LEDSliderBlue, cl.BlueLight, cl.SCHEME_BLUE),
        (cl.LEDSliderWhite, cl.WhiteLight, cl.SCHEME_WHITE),
    ):
        slider = slider_cls()
        try:
            light = slider.get_light()
            assert isinstance(light, cl.VCVSliderLight[light_cls])
            assert light.base_colors == [color]
        finally:
            slider.close()


def test_light_sliders_and_buttons(module: Module) -> None:
    slider = cl.LEDSliderGreen()
    try:
        light = slider.get_light()
        assert isinstance(slider, cl.VCVLightSlider[cl.GreenLight]) and isinstance(slider, cl.VCVSlider)
        assert isinstance(light, cl.VCVSliderLight[cl.GreenLight]) and light.base_colors == [cl.SCHEME_GREEN]
        slider.step()  # Rack's LightSlider moves the light to the handle's centre
        handle = slider.handle.get_box()
        centre = light.box.pos.plus(light.box.size.div(2))
        assert centre.x == pytest.approx(handle.pos.x + handle.size.x / 2)
    finally:
        slider.close()
    # A sized light, as Rack's plugins use: a plain RedLight keeps Widget's infinite size, as in C++.
    button = raw.create_light_param_centered(
        cl.VCVLightButton[cl.MediumSimpleLight[cl.RedLight]], Vec(20, 20), module, 0, 2
    )
    try:
        light = button.get_light()
        assert light.module is module and light.first_light_id == 2 and button.module is module
        assert light.box.pos.plus(light.box.size.div(2)) == button.box.size.div(2)  # centred
        assert cl.VCVLightLatch[cl.RedLight]().latch and cl.MomentarySwitch[cl.CKSS]().momentary
        bezel = cl.VCVLightBezel.default()()
        assert isinstance(bezel.get_light(), cl.VCVBezelLight[cl.WhiteLight]) and isinstance(bezel, cl.VCVBezel)
        latch = cl.VCVLightBezelLatch[cl.BlueLight]()
        assert latch.latch and not latch.momentary
        with pytest.raises(TypeError, match="get_light"):
            raw.create_light_param(app.Knob, Vec(), module, 1, 0)
    finally:
        button.close()


def test_segment_display(module: Module) -> None:
    display = cl.SegmentDisplay()
    display.box = Rect(Vec(), Vec(30, 6))
    try:
        assert display.lights_len == 0 and not display.vertical and display.margin == pytest.approx(mm2px(0.5))
        display.set_lights(module, 1, 3, cl.RedLight)  # Rack's setLights<RedLight>
        lights: list[Any] = list(display.children)
        assert len(lights) == 3 and display.lights_len == 3
        assert all(isinstance(light, cl.RectangleLight[cl.RedLight]) and light.module is module for light in lights)
        assert [light.first_light_id for light in lights] == [1, 2, 3]
        image = render(display, Vec(30, 6))
        assert pixel(image, 0, 0)[:3] == (0, 0, 0)  # Rack's black background
    finally:
        display.close()


def test_component_ports_reach_their_module(module: Module) -> None:
    port = raw.create_output_centered(cl.PJ301MPort, Vec(10, 10), module, 0)
    try:
        assert port.type == Port.Type.OUTPUT and port.get_port() is not None
    finally:
        port.close()
