# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.dsp.kernel import Gain, Sine
from rackpy.engine import Module


def voice(module: Module) -> None:
    module.config(num_params=2, num_inputs=1, num_outputs=2)
    module.config_param(0, -2, 2, 0, "Pitch", " V")
    module.config_param(1, 0, 1, 0.5, "Level")
    module.config_input(0, "V/oct")
    module.config_output(0, "Sine")
    # Rack's engine runs this native kernel every frame; Python never runs there.
    module.set_kernel(Sine(pitch_input=0, pitch_param=0, output=0, amplitude=5.0))


module = Module.create(voice)
try:
    kernel = module.kernel
    assert kernel is not None and kernel.type is Sine
    kernel.set("amplitude", 2.5)  # a control: reaches the kernel at a frame boundary
    assert kernel.values["amplitude"] == 2.5
    module.set_kernel(Gain(input=0, output=1, param=1))  # replaces the Sine kernel
    assert not kernel.alive and module.kernel is not None and module.kernel.type is Gain
finally:
    module.close()
