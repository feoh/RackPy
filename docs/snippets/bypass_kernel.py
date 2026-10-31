# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.dsp.graph import Graph
from rackpy.engine import Module

through = Graph()
through.output(0, through.input(0) * through.param(0))
bypassed = Graph()
bypassed.output(0, -bypassed.input(0))  # bypassed: the input, inverted, at unity gain


def inverter(module: Module) -> None:
    module.config(num_params=1, num_inputs=1, num_outputs=1)
    module.config_param(0, 0, 1, 1, "Level")
    module.set_kernel(through.compile())
    # Rack's engine runs this one instead of copying bypass routes while the module is bypassed.
    module.set_bypass_kernel(bypassed.compile())


module = Module.create(inverter)
try:
    kernel, bypass = module.kernel, module.bypass_kernel
    assert kernel is not None and bypass is not None and bypass is not kernel
    assert not module.bypass_kernel_failed
finally:
    module.close()
