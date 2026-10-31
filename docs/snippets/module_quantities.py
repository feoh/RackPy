# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module
from rackpy.raw.engine import SwitchQuantityView


def configure(module: Module) -> None:
    module.config(num_params=3, num_inputs=0, num_outputs=0)
    module.config_param(0, 0, 1, 0.5, "Level", "%", display_multiplier=100)
    module.config_switch(1, 0, 2, 1, "Mode", ["Low", "Middle", "High"])
    module.config_button(2, "Trigger")


module = Module.create(configure)
try:
    level = module.get_param_quantity(0)
    assert level is module.param_quantities[0]
    assert level.get_param() is module.params[0]
    assert level.module is module
    assert level.display_multiplier == 100
    # Metadata/defaults work even at plugin startup, before Rack creates its engine.
    param = level.get_param()  # None once the module is gone
    assert param is not None and param.value == level.default_value == 0.5
    assert level.get_range() == 1 and level.to_scaled(0.5) == 0.5
    mode = module.get_param_quantity(1)
    assert isinstance(mode, SwitchQuantityView)
    labels = mode.labels  # a checked write-through alias, not a list copy
    labels[0] = "Bass"
    labels.append("Extra")
    assert mode.labels.tolist() == ["Bass", "Middle", "High", "Extra"]
    mode.labels = ["First", "Second"]
    assert list(labels) == ["First", "Second"]  # same retained alias
finally:
    module.close()
assert not level.alive
