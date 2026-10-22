# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module


def wire(module: Module) -> None:
    module.config(num_params=1, num_inputs=1, num_outputs=1, num_lights=1)
    module.config_bypass(input_id=0, output_id=0)


module = Module.create(wire)
try:
    module.params[0].value = 0.5
    assert module.get_param(0) is module.params[0]
finally:
    module.close()
