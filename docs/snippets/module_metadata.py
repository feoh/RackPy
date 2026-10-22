# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module
from rackpy.raw.engine import LightInfo, PortInfo


def describe(module: Module) -> None:
    module.config(num_params=0, num_inputs=1, num_outputs=1, num_lights=1)
    module.config_input(0, "Pitch").description = "Pitch control in volts."
    module.config_output(0, "Signal")
    module.config_light(0, "Activity")


module = Module.create(describe)
standalone_port, standalone_light = PortInfo(), LightInfo()
standalone_port.module = standalone_light.module = module
assert standalone_port.module is standalone_light.module is module
try:
    info = module.get_input_info(0)
    assert info is module.input_infos[0]
    assert info.module is module
    assert info.get_full_name() == "Pitch input"
finally:
    module.close()
assert not info.alive
assert standalone_port.module is standalone_light.module is None
standalone_port.name = "Independent"
assert standalone_port.get_name() == "Independent"
