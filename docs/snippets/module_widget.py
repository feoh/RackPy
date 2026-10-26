# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack modules=PyBlink
from rackpy import raw
from rackpy.engine import Module
from rackpy.plugin import plugin
from rackpy.raw import app
from rackpy.raw.math import Vec
from rackpy.raw.window import Svg

# Original artwork, inline: a 4 HP panel and a jack.
PANEL = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="60" height="380">'
    '<rect width="60" height="380" fill="#283038"/><rect x="6" y="6" width="48" height="14" fill="#48525e"/></svg>'
)
JACK = (
    '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24">'
    '<circle cx="12" cy="12" r="11" fill="#aab2bc"/><circle cx="12" cy="12" r="5" fill="#0e1216"/></svg>'
)


def svg(text: str) -> Svg:
    image = Svg()
    image.load_string(text)
    return image


def blink(module: Module) -> None:
    module.config(1, 0, 1, 1)
    module.config_param(0, 0.1, 10, 1, "Rate", " Hz")
    module.config_output(0, "Pulse")
    module.config_light(0, "Blink")


class Jack(app.SvgPort):
    def __init__(self) -> None:
        super().__init__()
        self.set_svg(svg(JACK))


class Rate(app.Knob):
    def __init__(self) -> None:
        super().__init__()
        self.box.size = Vec(26, 26)


class Lamp(app.ModuleLightWidget):
    def __init__(self) -> None:
        super().__init__()
        self.box.size = Vec(8, 8)
        self.add_base_color(raw.nanovg.rgb(255, 196, 64))


class BlinkWidget(app.ModuleWidget):
    """Rack calls this with the module, or None for its module browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)  # first, as Rack's module widgets do
        self.set_panel(svg(PANEL))
        self.add_param(raw.create_param_centered(Rate, Vec(30, 90), module, 0))
        self.add_child(raw.create_light_centered(Lamp, Vec(30, 150), module, 0))
        self.add_output(raw.create_output_centered(Jack, Vec(30, 300), module, 0))


plugin.add_model("PyBlink", blink, widget=BlinkWidget)
