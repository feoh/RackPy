# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module, Port, PortInfo
from rackpy.raw.engine import Light


def build(module: Module) -> None:
    module.config(num_params=0, num_inputs=2, num_outputs=0, num_lights=0)
    # Drop the second input, port and metadata together, as C++ would.
    del module.inputs[1]
    del module.input_infos[1]
    # Replace the first input's metadata with an object of our own. A raw
    # assignment sets no fields, so set what the seal checks.
    info = PortInfo()
    info.module, info.port_id, info.type, info.name = module, 0, Port.Type.INPUT, "Clock"
    module.input_infos[0] = info
    # Add a light and an empty (None) metadata slot for it.
    module.lights.append(Light())
    module.light_infos.append(None)


module = Module.create(build)
try:
    assert len(module.inputs) == len(module.input_infos) == 1
    assert module.get_input_info(0).get_name() == "Clock"
    assert module.get_num_lights() == 1 and module.get_light_info(0) is None
finally:
    module.close()
