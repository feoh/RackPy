# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy import laboratory
from rackpy.engine import Module
from rackpy.laboratory import Bench, ModuleClass, ProcessArgs

laboratory.enable()  # trusted code only; patches never enable it


class Doubler(ModuleClass):
    def configure(self) -> None:
        self.module.config(0, 1, 1, 0)

    def process(self, args: ProcessArgs) -> None:  # offline: on the bench, never in Rack's engine
        inp, out = self.module.inputs[0], self.module.outputs[0]
        out.set_channels(inp.get_channels())
        for c in range(inp.get_channels()):
            out.set_voltage(2 * inp.get_voltage(c), c)


try:
    bench = Bench(48000)
    source = Module.create(lambda m: m.config(0, 0, 1, 0))
    doubler = Doubler.create()
    bench.add(source)
    bench.add(doubler.module)
    bench.connect(source, 0, doubler.module, 0)
    source.outputs[0].set_voltage(1.5)
    bench.step(2)  # the cable delivers the source's voltage one frame later
    assert doubler.module.outputs[0].get_voltage() == 3.0
    bench.clear()
finally:
    laboratory.disable()
