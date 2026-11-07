# SPDX-License-Identifier: GPL-3.0-or-later
"""A mono feedback loop with bounded return, tone control and a DC blocker."""

from enum import IntEnum

from rackpy import assets, raw
from rackpy.dsp.kernel import load
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    FEEDBACK = 0
    CAP = 1
    TONE = 2
    DECAY = 3
    KILL = 4


class Input(IntEnum):
    SEND = 0
    RETURN = 1
    CV = 2
    KILL = 3


class Output(IntEnum):
    GOVERNED = 0


def configure(module: Module) -> None:
    """Configure the module and attach its native per-sample kernel."""
    module.config(len(Param), len(Input), len(Output))
    module.config_param(Param.FEEDBACK, 0.0, 1.0, 0.35, "Feedback", "%", 0.0, 100.0)
    module.config_param(Param.CAP, 1.0, 10.0, 5.0, "Output cap", " V")
    module.config_param(Param.TONE, 20.0, 20000.0, 4000.0, "Return tone", " Hz", 2.0, 20.0)
    module.config_param(Param.DECAY, 0.0, 1.0, 0.9, "Per-pass decay", "%", 0.0, 100.0)
    module.config_button(Param.KILL, "Kill / reset")
    module.config_input(Input.SEND, "Send / dry signal")
    module.config_input(Input.RETURN, "Feedback return")
    module.config_input(Input.CV, "Feedback amount CV (0–10 V)")
    module.config_input(Input.KILL, "Kill gate")
    module.config_output(Output.GOVERNED, "Governed loop output")
    governor = load("governor.FeedbackGovernor")
    module.set_kernel(
        governor(
            feedback=Param.FEEDBACK,
            cap=Param.CAP,
            tone=Param.TONE,
            decay=Param.DECAY,
            kill_button=Param.KILL,
            send=Input.SEND,
            **{"return": Input.RETURN},
            cv=Input.CV,
            kill_input=Input.KILL,
            output=Output.GOVERNED,
        )
    )


class FeedbackGovernorWidget(app.ModuleWidget):
    """Original panel; Rack passes None for the browser preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/FeedbackGovernor.svg"))))
        right, bottom = self.box.size.x - 2 * app.RACK_GRID_WIDTH, app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        for x, y in ((app.RACK_GRID_WIDTH, 0), (right, 0), (app.RACK_GRID_WIDTH, bottom), (right, bottom)):
            self.add_child(raw.create_widget(cl.ScrewSilver, Vec(x, y)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(15.24, 26.0)), module, Param.FEEDBACK))
        self.add_param(raw.create_param_centered(cl.RoundSmallBlackKnob, mm2px(Vec(15.24, 46.0)), module, Param.CAP))
        self.add_param(raw.create_param_centered(cl.RoundSmallBlackKnob, mm2px(Vec(15.24, 65.0)), module, Param.TONE))
        self.add_param(raw.create_param_centered(cl.RoundSmallBlackKnob, mm2px(Vec(15.24, 84.0)), module, Param.DECAY))
        self.add_param(raw.create_param_centered(cl.VCVButton, mm2px(Vec(15.24, 101.0)), module, Param.KILL))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(7.0, 114.0)), module, Input.SEND))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(15.24, 114.0)), module, Input.RETURN))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(23.48, 114.0)), module, Input.CV))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(7.0, 124.0)), module, Output.GOVERNED))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(23.48, 124.0)), module, Input.KILL))
