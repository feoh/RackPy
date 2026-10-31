"""Splitter: a signal and its inverse, with a custom bypass.

Generated from res/Splitter.svg by bin/rackpy-plugin add-module, then edited. Rack's
bypass routes only copy an input to an output, which would make a bypassed Splitter's
INVERTED output a copy of its input. This module instead gives Rack a second native
kernel to run while it is bypassed (C++'s processBypass override): unity gain, with
INVERTED still inverted.
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
    LEVEL = dsp.SPLIT_LEVEL


class Input(IntEnum):
    SIGNAL = dsp.SPLIT_SIGNAL


class Output(IntEnum):
    THROUGH = dsp.THROUGH
    INVERTED = dsp.INVERTED


GRAPH = dsp.splitter_graph()
BYPASS_GRAPH = dsp.splitter_bypass_graph()


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output))
    module.config_param(Param.LEVEL, 0.0, 1.0, 1.0, "Level", "%", 0.0, 100.0)
    module.config_input(Input.SIGNAL, "Signal")
    module.config_output(Output.THROUGH, "Through")
    module.config_output(Output.INVERTED, "Inverted")
    module.get_output_info(Output.INVERTED).description = "Bypassed, still inverted, at unity gain."
    module.set_kernel(GRAPH.compile())
    module.set_bypass_kernel(BYPASS_GRAPH.compile())  # instead of bypass routes


class SplitterWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Splitter.svg"))))
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 32.0)), module, Param.LEVEL))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(15.24, 60.0)), module, Input.SIGNAL))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(15.24, 83.0)), module, Output.THROUGH))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(15.24, 110.0)), module, Output.INVERTED))
