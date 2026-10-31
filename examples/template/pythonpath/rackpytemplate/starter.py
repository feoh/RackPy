"""Starter: generated from res/Starter.svg by bin/rackpy-plugin add-module.

Configure the controls in configure(), and give the module a kernel for its audio
(rackpy.dsp.graph or rackpy.dsp.kernel): Python never runs per sample.
"""

from enum import IntEnum

from rackpy import assets, raw
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw import widget as w
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    LEVEL = 0


class Input(IntEnum):
    IN = 0


class Output(IntEnum):
    OUT = 0


class Light(IntEnum):
    ACTIVE = 0


class Readout(w.Widget):
    """A custom widget on the panel: draw it here."""


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output), len(Light))
    module.config_param(Param.LEVEL, 0.0, 1.0, 0.0, "")
    module.config_input(Input.IN, "")
    module.config_output(Output.OUT, "")


class StarterWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Starter.svg"))))
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 30.0)), module, Param.LEVEL))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(15.24, 72.0)), module, Input.IN))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(15.24, 84.0)), module, Output.OUT))
        self.add_child(
            raw.create_light_centered(cl.MediumLight[cl.RedLight], mm2px(Vec(15.24, 46.0)), module, Light.ACTIVE)
        )
        custom = raw.create_widget(Readout, mm2px(Vec(5.24, 104.0)))
        custom.box.size = mm2px(Vec(20.0, 8.0))
        self.add_child(custom)
