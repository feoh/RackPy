# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module, ParamQuantity, SwitchQuantity


class Decibels(ParamQuantity):
    """Rack shows this label in the knob's tooltip and menu."""

    def __init__(self) -> None:
        super().__init__()
        self.description = "Output level."
        self.display_precision = 3

    def get_label(self) -> str:
        return self.name + " (dB)"


class Waveform(SwitchQuantity):
    def __init__(self) -> None:
        super().__init__()
        self.randomize_enabled = False  # config_switch keeps this


def describe(module: Module) -> None:
    module.config(num_params=2, num_inputs=0, num_outputs=0)
    module.config_param(0, -60, 6, 0, "Level", quantity_type=Decibels)
    module.config_switch(1, 0, 2, 0, "Wave", ["Sine", "Triangle", "Saw"], quantity_type=Waveform)


module = Module.create(describe)
try:
    level, wave = module.get_param_quantity(0), module.get_param_quantity(1)
    assert isinstance(level, Decibels) and level.get_label() == "Level (dB)"
    assert isinstance(wave, Waveform) and list(wave.labels) == ["Sine", "Triangle", "Saw"]
    assert wave.snap_enabled and not wave.randomize_enabled
    assert module.params[0].value == 0  # the Param starts at the default
finally:
    module.close()
assert not level.alive and not wave.alive
