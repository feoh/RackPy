"""PolySine and PolySineScalar: a polyphonic sine oscillator, as two native kernels.

Generated from res/PolySine.svg by bin/rackpy-plugin add-module, then edited. Both
models have the same controls and panel; they differ in the kernel Rack's engine runs,
from this plugin's C++ kernel library (kernels/polyphony.cpp, which RackPy loads before
this package because rackpy-plugin.json lists it):

- PolySine: "polyphony.Simd", four voices at a time in rack::simd::float_4;
- PolySineScalar: "polyphony.Scalar", one voice at a time.

A cable with N channels into V/OCT gives N voices and an N-channel output; unpatched
gives one voice at the knob's pitch. Python only configures; it never runs per sample.
rackpypolyphony_teaching shows the same loops in Python, offline.
"""

from collections.abc import Callable
from enum import IntEnum

from rackpy import assets, raw
from rackpy.dsp.kernel import Kernel, load
from rackpy.engine import Module
from rackpy.raw import app
from rackpy.raw import componentlibrary as cl
from rackpy.raw.dsp import FREQ_C4
from rackpy.raw.math import Vec
from rackpy.raw.window import mm2px


class Param(IntEnum):
    PITCH = 0


class Input(IntEnum):
    VOCT = 0


class Output(IntEnum):
    SINE = 0


def configure_with(kernel_type: str) -> Callable[[Module], None]:
    """A model's constructor callback, giving each module a kernel of `kernel_type`."""
    oscillator = load(kernel_type)

    def configure(module: Module) -> None:
        """Rack calls this to build each module: configure its controls here."""
        module.config(len(Param), len(Input), len(Output))
        module.config_param(Param.PITCH, -3.0, 3.0, 0.0, "Pitch", " Hz", 2.0, FREQ_C4)
        module.config_input(Input.VOCT, "1V/octave pitch, one channel per voice")
        module.config_output(Output.SINE, "Sine, one channel per voice")
        kernel: Kernel = oscillator(pitch=Param.PITCH, voct=Input.VOCT, sine=Output.SINE, amplitude=5.0)
        module.set_kernel(kernel)

    return configure


simd = configure_with("polyphony.Simd")
scalar = configure_with("polyphony.Scalar")


class PolySineWidget(app.ModuleWidget):
    """The module's panel. Rack passes the module, or None for its browser's preview."""

    def __init__(self, module: Module | None) -> None:
        super().__init__()
        self.set_module(module)
        self.set_panel(raw.create_panel(str(assets.plugin("res/PolySine.svg"))))
        right, bottom = self.box.size.x - 2 * app.RACK_GRID_WIDTH, app.RACK_GRID_HEIGHT - app.RACK_GRID_WIDTH
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(app.RACK_GRID_WIDTH, 0)))
        self.add_child(raw.create_widget(cl.ScrewSilver, Vec(right, bottom)))
        self.add_param(raw.create_param_centered(cl.RoundBlackKnob, mm2px(Vec(10.16, 40.0)), module, Param.PITCH))
        self.add_input(raw.create_input_centered(cl.PJ301MPort, mm2px(Vec(10.16, 82.0)), module, Input.VOCT))
        self.add_output(raw.create_output_centered(cl.PJ301MPort, mm2px(Vec(10.16, 106.0)), module, Output.SINE))
