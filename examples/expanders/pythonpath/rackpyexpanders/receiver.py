"""Receiver: outputs what a Source on its left sends, one frame later.

Generated from res/Receiver.svg by bin/rackpy-plugin add-module, then edited. A module
receives in its own messages, so configure() gives its left side two buffers of the
"rackpy.graph" schema with room for the Source's two lanes, before Rack's engine runs
it, as a C++ module allocates them in its constructor. Its graph reads them. RackPy
checks the neighbour on the engine thread: a module that is not a RackPy module, or
whose messages do not fit, sends nothing, and the Receiver outputs 0 channels.
"""

from enum import IntEnum

import rackpyexpanders_graphs as graphs

from rackpy import assets, raw
from rackpy.engine import Module
from rackpy.engine.expanders import graph_messages
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Output(IntEnum):
    RECEIVED = graphs.RECEIVED


class Light(IntEnum):
    LINK = graphs.LINK


GRAPH = graphs.receiver_graph()


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(0, 0, len(Output), len(Light))
    module.config_output(Output.RECEIVED, "Received signal")
    module.config_light(Light.LINK, "Source's level")
    graph_messages(module.left_expander, graphs.LANES)  # the buffers the Source writes into
    module.set_kernel(GRAPH.compile())


class ReceiverWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Receiver.svg"))))
        right, bottom = self.box.size.x - 2 * app.RACK_GRID_WIDTH, app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(app.RACK_GRID_WIDTH, 0)))
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(right, bottom)))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(10.16, 106.0)), module, Output.RECEIVED))
        self.add_child(
            raw.create_light_centered(cl.MediumLight[cl.BlueLight], mm2px(Vec(10.16, 40.0)), module, Light.LINK)
        )
