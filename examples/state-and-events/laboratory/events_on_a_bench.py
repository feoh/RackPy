"""The sequencer's events in Python, offline: RackPy's laboratory equivalent.

Rack's engine dispatches the sample-rate event on its audio threads, where RackPy never
runs Python, so the plugin's kernel handles it natively. To experiment with Python
callbacks for every event, a laboratory module runs on a Bench, an offline host that
Python steps on Rack's main thread and that dispatches events as Rack's engine does.
This is not real-time audio. Run it inside Rack (trusted code); it prints what it saw.
"""

import math

from rackpy import laboratory
from rackpy.engine import Module
from rackpy.laboratory import Bench, ModuleClass, ProcessArgs


class Glide(ModuleClass):
    """Slews its output towards its input with a 10 ms time constant, whatever the sample rate."""

    TIME = 0.01

    def configure(self) -> None:
        self.module.config(0, 1, 1)
        self.events: list[str] = []
        self.level = 0.0
        self.coefficient = 0.0

    def on_sample_rate_change(self, event: Module.SampleRateChangeEvent) -> None:
        # A per-sample coefficient that depends on the sample rate, recomputed when it changes.
        self.coefficient = 1.0 - math.exp(-event.sample_time / self.TIME)
        self.events.append(f"rate {event.sample_rate:g}")

    def on_reset(self, event: Module.ResetEvent) -> None:
        self.level = 0.0
        self.events.append("reset")

    def on_randomize(self, event: Module.RandomizeEvent) -> None:
        self.events.append("randomize")

    def process(self, args: ProcessArgs) -> None:
        self.level += (self.module.inputs[0].get_voltage() - self.level) * self.coefficient
        self.module.outputs[0].set_voltage(self.level)


def main() -> dict[str, object]:
    laboratory.enable()  # trusted code only; patches never enable it
    try:
        bench = Bench(48000)
        glide = Glide.create()
        source = Module.create(lambda m: m.config(0, 0, 1))
        bench.add(source)
        bench.add(glide.module)  # AddEvent, then SampleRateChangeEvent
        bench.connect(source, 0, glide.module, 0)
        source.outputs[0].set_voltage(1.0)
        bench.step(481)  # 10 ms after the cable's one-frame delay
        at_48k = glide.module.outputs[0].get_voltage()
        bench.reset_module(glide.module)
        bench.set_sample_rate(96000)
        bench.step(961)
        at_96k = glide.module.outputs[0].get_voltage()
        bench.randomize_module(glide.module)
        bench.clear()
        return {"events": glide.events, "at_48k": at_48k, "at_96k": at_96k}
    finally:
        laboratory.disable()


if __name__ == "__main__":
    print(main())
