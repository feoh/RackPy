"""Source: scales its input by LEVEL and sends the result, and LEVEL, to the module on its right.

Generated from res/Source.svg by bin/rackpy-plugin add-module, then edited. The sending
is part of its graph (rackpyexpanders_graphs.source_graph), a native kernel: Rack's
engine writes the right neighbour's message every frame and requests the flip. The
receiving module owns the message buffers, so the Source allocates none.
"""

from enum import IntEnum

import rackpyexpanders_graphs as graphs

from rackpy import assets, raw
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    LEVEL = graphs.LEVEL


class Input(IntEnum):
    SIGNAL = graphs.SIGNAL


class Output(IntEnum):
    SCALED = graphs.SCALED


GRAPH = graphs.source_graph()


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output))
    module.config_param(Param.LEVEL, 0.0, 1.0, 1.0, "Level", "%", 0.0, 100.0)
    module.config_input(Input.SIGNAL, "Signal")
    module.config_output(Output.SCALED, "Scaled signal")
    module.set_kernel(GRAPH.compile())


class SourceWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Source.svg"))))
        right, bottom = self.box.size.x - 2 * app.RACK_GRID_WIDTH, app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((app.RACK_GRID_WIDTH, 0), (right, 0), (app.RACK_GRID_WIDTH, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 40.0)), module, Param.LEVEL))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(15.24, 82.0)), module, Input.SIGNAL))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(15.24, 106.0)), module, Output.SCALED))
