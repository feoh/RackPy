"""Filter: a polyphonic biquad filter running natively, from Rack's own DSP library.

Generated from res/Filter.svg by bin/rackpy-plugin add-module, then edited. Its kernel,
"dsp.Biquad" from this plugin's kernel library (kernels/dsp.cpp), runs Rack's
TBiquadFilter<float_4> on four channels at a time and recomputes the coefficients every 32
frames with a ClockDivider. Python only configures; rackpydsp_teaching runs the same Rack
filter code from Python, offline, to show and check what the kernel does.
"""

from enum import IntEnum

from rackpy import assets, raw
from rackpy.dsp.kernel import load
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px

MODES = ("Lowpass", "Highpass", "Bandpass", "Notch")


class Param(IntEnum):
    CUTOFF = 0
    RESONANCE = 1
    MODE = 2


class Input(IntEnum):
    SIGNAL = 0


class Output(IntEnum):
    FILTERED = 0


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output))
    # Octaves above 20 Hz, shown as hertz: 20 Hz * 2^value, from 20 Hz to 20.48 kHz.
    module.config_param(Param.CUTOFF, 0.0, 10.0, 5.0, "Cutoff", " Hz", 2.0, 20.0)
    module.config_param(Param.RESONANCE, 0.5, 10.0, 0.707, "Resonance (Q)")
    module.config_switch(Param.MODE, 0.0, 3.0, 0.0, "Mode", list(MODES))
    module.config_input(Input.SIGNAL, "Signal")
    module.config_output(Output.FILTERED, "Filtered signal")
    module.config_bypass(Input.SIGNAL, Output.FILTERED)
    biquad = load("dsp.Biquad")
    module.set_kernel(
        biquad(
            cutoff=Param.CUTOFF, resonance=Param.RESONANCE, mode=Param.MODE, input=Input.SIGNAL, output=Output.FILTERED
        )
    )


class FilterWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Filter.svg"))))
        right, bottom = self.box.size.x - 2 * app.RACK_GRID_WIDTH, app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((app.RACK_GRID_WIDTH, 0), (right, 0), (app.RACK_GRID_WIDTH, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 30.0)), module, Param.CUTOFF))
        self.add_param(
            raw.create_param_centered(cl.RoundSmallBlackKnob, mm2px(Vec(15.24, 54.0)), module, Param.RESONANCE)
        )
        self.add_param(raw.create_param_centered(cl.Trimpot, mm2px(Vec(15.24, 74.0)), module, Param.MODE))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(9.0, 104.0)), module, Input.SIGNAL))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(21.48, 104.0)), module, Output.FILTERED))
