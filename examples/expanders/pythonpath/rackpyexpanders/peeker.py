"""Peeker: reads a Source on its left directly, without messages.

Generated from res/Peeker.svg by bin/rackpy-plugin add-module, then edited. Its kernel,
"expanders.Peek" from this plugin's kernel library (kernels/expanders.cpp), checks on
Rack's engine thread that the module on its left is this plugin's Source, then reads that
module's LEVEL knob and output, as a C++ module reads Rack's leftExpander.module. LEVEL
outputs the knob as 0 to 10 V and SIGNAL copies the Source's output; the light is on
while a Source is there. What it reads is the Source's value of this frame or the last,
depending on Rack's processing order; the Receiver's messages are always one frame late.
"""

from enum import IntEnum

from rackpy import assets, raw
from rackpy.dsp.kernel import load
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Output(IntEnum):
    LEVEL = 0
    SIGNAL = 1


class Light(IntEnum):
    SEEN = 0


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(0, 0, len(Output), len(Light))
    module.config_output(Output.LEVEL, "The Source's level, 0 to 10 V")
    module.config_output(Output.SIGNAL, "The Source's output")
    module.config_light(Light.SEEN, "Source on the left")
    peek = load("expanders.Peek")
    module.set_kernel(peek(level=Output.LEVEL, signal=Output.SIGNAL, seen=Light.SEEN))


class PeekerWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Peeker.svg"))))
        right, bottom = self.box.size.x - 2 * app.RACK_GRID_WIDTH, app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(app.RACK_GRID_WIDTH, 0)))
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(right, bottom)))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(10.16, 82.0)), module, Output.LEVEL))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(10.16, 106.0)), module, Output.SIGNAL))
        self.add_child(
            raw.create_light_centered(cl.MediumLight[cl.GreenLight], mm2px(Vec(10.16, 40.0)), module, Light.SEEN)
        )
