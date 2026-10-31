"""Shaper: each way Rack configures and displays a module's controls, ports and bypass.

Generated from res/Shaper.svg by bin/rackpy-plugin add-module, then edited: configure()
gives every parameter its range, name and display (plain, in volts, as a percentage,
as hertz on an exponential scale, in decibels), a labelled switch and a button,
descriptions for the tooltips, names for the ports and the light, and a bypass route.
The audio is rackpyconfiguration_dsp's graph, compiled into a native kernel.
"""

from enum import IntEnum

import rackpyconfiguration_dsp as dsp

from rackpy import assets, raw
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    LEVEL = dsp.LEVEL
    SCALE = dsp.SCALE
    OFFSET = dsp.OFFSET
    DEPTH = dsp.DEPTH
    RATE = dsp.RATE
    MODE = dsp.MODE
    MUTE = dsp.MUTE


class Input(IntEnum):
    SIGNAL = dsp.SIGNAL


class Output(IntEnum):
    SHAPED = dsp.SHAPED


class Light(IntEnum):
    LFO = dsp.LFO


GRAPH = dsp.shaper_graph()


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output), len(Light))
    # A gain, shown in decibels: Rack shows 20 * log10(value) for a negative base of -10.
    module.config_param(Param.LEVEL, 0.0, 2.0, 1.0, "Level", " dB", -10.0, 20.0)
    # Plain and linear: the tooltip is the value itself.
    module.config_param(Param.SCALE, -1.0, 1.0, 1.0, "Scale")
    # Linear, with a unit.
    module.config_param(Param.OFFSET, -5.0, 5.0, 0.0, "Offset", " V")
    # A fraction, shown as a percentage.
    module.config_param(Param.DEPTH, 0.0, 1.0, 0.0, "Tremolo depth", "%", 0.0, 100.0)
    # Octaves, shown as hertz: 1 Hz * 2^value.
    rate = module.config_param(Param.RATE, -3.0, 3.0, 0.0, "Tremolo rate", " Hz", 2.0, 1.0)
    rate.description = "Doubles with each sixth of a turn; the light follows the tremolo."
    # A switch shows its position's label; a button is a momentary switch, 0 or 1.
    module.config_switch(Param.MODE, 0.0, 2.0, 0.0, "Shape", ["Bipolar", "Rectified", "Positive half"])
    module.config_button(Param.MUTE, "Mute while held")
    module.config_input(Input.SIGNAL, "Signal")
    shaped = module.config_output(Output.SHAPED, "Shaped")
    shaped.description = "Shape, then level, scale and tremolo, plus offset; polyphonic like the input."
    module.config_light(Light.LFO, "Tremolo")
    # Bypassed, Rack copies the input to the output, channels and all.
    module.config_bypass(Input.SIGNAL, Output.SHAPED)
    module.set_kernel(GRAPH.compile())


class ShaperWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Shaper.svg"))))
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(14.0, 29.0)), module, Param.LEVEL))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(36.8, 29.0)), module, Param.SCALE))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(14.0, 49.0)), module, Param.OFFSET))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(36.8, 49.0)), module, Param.DEPTH))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(14.0, 80.0)), module, Param.RATE))
        self.add_param(raw.create_param_centered(cl.CKSSThree, mm2px(Vec(36.8, 80.0)), module, Param.MODE))
        self.add_param(raw.create_param_centered(cl.VCVButton, mm2px(Vec(25.4, 91.0)), module, Param.MUTE))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(12.0, 111.0)), module, Input.SIGNAL))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(38.8, 111.0)), module, Output.SHAPED))
        self.add_child(
            raw.create_light_centered(cl.MediumLight[cl.GreenLight], mm2px(Vec(25.4, 70.0)), module, Light.LFO)
        )
