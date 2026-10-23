# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module
from rackpy.raw.engine import LightInfo, PortInfo


class VoltPort(PortInfo):
    """Rack shows this name in the port's tooltip."""

    def __init__(self) -> None:
        super().__init__()
        self.description = "Volts per octave."

    def get_name(self) -> str:
        return self.name + " (1V/oct)"


def describe(module: Module) -> None:
    module.config(num_params=0, num_inputs=1, num_outputs=0, num_lights=1)
    pitch = module.config_input(0, "Pitch", info_type=VoltPort)
    assert pitch.get_full_name() == "Pitch (1V/oct) input"
    module.config_light(0, "Activity", info_type=LightInfo)


module = Module.create(describe)
try:
    pitch = module.get_input_info(0)
    assert isinstance(pitch, VoltPort) and pitch is module.input_infos[0]
    assert pitch.module is module and pitch.get_description() == "Volts per octave."
finally:
    module.close()
# The slot owned the object; Python still has it, but it is stale now.
assert not pitch.alive
