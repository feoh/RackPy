"""Oscillator: a sine oscillator with a 1 Hz blink, the feature of Rack's tutorial module.

Generated from res/Oscillator.svg by bin/rackpy-plugin add-module, then edited: configure()
sets the controls' ranges and names and gives each module a native kernel compiled from
rackpyoscillator_dsp's graph. Rack's engine runs that kernel every sample; Python only
configures it, on Rack's main thread.
"""

from enum import IntEnum

from rackpyoscillator_dsp import oscillator_graph

from rackpy import assets, raw
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.dsp import FREQ_C4
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    PITCH = 0


class Input(IntEnum):
    PITCH = 0


class Output(IntEnum):
    SINE = 0


class Light(IntEnum):
    BLINK = 0


GRAPH = oscillator_graph(Param.PITCH, Input.PITCH, Output.SINE, Light.BLINK)


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output), len(Light))
    # The knob is in volts (0 V is middle C); Rack shows it as 261.63 Hz * 2^volts.
    module.config_param(Param.PITCH, -3.0, 3.0, 0.0, "Frequency", " Hz", 2.0, FREQ_C4)
    module.config_input(Input.PITCH, "1V/octave pitch")
    module.config_output(Output.SINE, "Sine")
    module.config_light(Light.BLINK, "Blink")
    module.set_kernel(GRAPH.compile())  # each module gets its own kernel and phase


class OscillatorWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Oscillator.svg"))))
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundLargeBlackKnob, mm2px(Vec(20.32, 38.0)), module, Param.PITCH))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(13.0, 90.0)), module, Input.PITCH))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(28.5, 90.0)), module, Output.SINE))
        self.add_child(
            raw.create_light_centered(cl.MediumLight[cl.RedLight], mm2px(Vec(20.32, 60.0)), module, Light.BLINK)
        )
