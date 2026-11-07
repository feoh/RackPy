"""A/D Envelope: generated from res/Envelope.svg by bin/rackpy-plugin add-module.

Configure the controls in configure(), and give the module a kernel for its audio
(rackpy.dsp.graph or rackpy.dsp.kernel): Python never runs per sample.
"""

from enum import IntEnum

from rackpy import assets, raw
from rackpy.dsp.kernel import load
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    ATTACK = 0
    DECAY = 1
    LOOP = 2


class Input(IntEnum):
    TRIGGER = 0


class Output(IntEnum):
    ENVELOPE = 0
    INVERTED = 1
    EOC = 2


class Light(IntEnum):
    pass


def configure(module: Module) -> None:
    """Rack calls this to build each module: configure its controls here."""
    module.config(len(Param), len(Input), len(Output), len(Light))
    module.config_param(Param.ATTACK, 0.0, 1.0, 0.35, "Attack", " s", 2000.0, 0.001)
    module.config_param(Param.DECAY, 0.0, 1.0, 0.5, "Decay", " s", 2000.0, 0.001)
    module.config_switch(Param.LOOP, 0.0, 1.0, 0.0, "Loop", ["Off", "On"])
    module.config_input(Input.TRIGGER, "Trigger")
    module.config_output(Output.ENVELOPE, "Envelope")
    module.config_output(Output.INVERTED, "Inverted envelope")
    module.config_output(Output.EOC, "End of cycle")
    kernel = load("envelope.AD")
    module.set_kernel(
        kernel(
            attack=Param.ATTACK,
            decay=Param.DECAY,
            loop=Param.LOOP,
            trigger=Input.TRIGGER,
            envelope=Output.ENVELOPE,
            inverted=Output.INVERTED,
            eoc=Output.EOC,
        )
    )


class EnvelopeWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/Envelope.svg"))))
        grid, right = app.RACK_GRID_WIDTH, self.box.size.x - 2 * app.RACK_GRID_WIDTH
        bottom = app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((grid, 0), (right, 0), (grid, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 42.0)), module, Param.ATTACK))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 63.0)), module, Param.DECAY))
        self.add_param(raw.create_param(cl.CKSS, mm2px(Vec(11.0, 78.0)), module, Param.LOOP))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(8.0, 105.0)), module, Input.TRIGGER))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(22.48, 105.0)), module, Output.ENVELOPE))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(8.0, 116.0)), module, Output.INVERTED))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(22.48, 116.0)), module, Output.EOC))
