# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack output=graph_kernel.out
from rackpy.dsp.graph import Graph
from rackpy.engine import Module

# Rack's tutorial oscillator as a graph: a sine at the pitch knob plus the V/oct
# input, and a light blinking at 1 Hz. "level" is a control.
g = Graph()
pitch = g.param(0) + g.input(0)
g.output(0, g.control("level", 5.0) * g.sin2pi(g.phasor(g.pitch_to_freq(pitch))))
g.light(0, g.phasor(1.0) < 0.5)


def oscillator(module: Module) -> None:
    module.config(num_params=1, num_inputs=1, num_outputs=1, num_lights=1)
    module.config_param(0, -2, 2, 0, "Pitch", " V")
    # The plan interpreter runs the graph natively every frame; Python never runs there.
    module.set_kernel(g.compile())


module = Module.create(oscillator)
try:
    kernel = module.kernel
    assert kernel is not None
    print(kernel.type.name, kernel.type.controls)
    print(sorted(kernel.values))
    kernel.set("level", 2.5)  # reaches the plan at a frame boundary
    print(kernel.values["level"])
finally:
    module.close()
