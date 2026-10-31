"""Lantern: a module with a light panel and a dark one.

Generated from res/Lantern.svg by bin/rackpy-plugin add-module, then edited. Rack shows
the dark panel, res/Lantern-dark.svg, while its "Use dark panels if available" setting
(View menu) is on, and the light one otherwise. The themed screws and output jack switch
the same way. The lamp is a light: Rack draws lights on its self-lit layer, so the lamp
stays bright when the room is dimmed (View > Room brightness), while the panel darkens.

GLOW sets the lamp's brightness, and LEVEL outputs it as 0 to 10 V; Rack's engine runs
that as a native kernel.
"""

from enum import IntEnum

from rackpy import assets, raw
from rackpy.dsp.graph import Graph
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    GLOW = 0


class Output(IntEnum):
    LEVEL = 0


class Light(IntEnum):
    LAMP = 0


def lantern_graph() -> Graph:
    g = Graph()
    glow = g.param(Param.GLOW)
    g.light(Light.LAMP, glow)
    g.output(Output.LEVEL, glow * 10.0)
    return g


GRAPH = lantern_graph()


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), 0, len(Output), len(Light))
    module.config_param(Param.GLOW, 0.0, 1.0, 0.8, "Glow", "%", 0.0, 100.0)
    module.config_output(Output.LEVEL, "Glow level")
    module.config_light(Light.LAMP, "Lamp")
    module.set_kernel(GRAPH.compile())


class LanternWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        # Two SVGs make a ThemedSvgPanel: Rack picks one by its dark-panel setting.
        self.set_panel(
            raw.create_panel(str(assets.plugin("res/Lantern.svg")), str(assets.plugin("res/Lantern-dark.svg")))
        )
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ThemedScrew, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 38.0)), module, Param.GLOW))
        self.add_output(raw.create_output_centered(cl.ThemedPJ301MPort, mm2px(Vec(15.24, 108.0)), module, Output.LEVEL))
        self.add_child(
            raw.create_light_centered(cl.LargeLight[cl.YellowLight], mm2px(Vec(15.24, 62.0)), module, Light.LAMP)
        )
