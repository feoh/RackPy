# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module


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
    assert level.get_param().value == level.default_value == 0.5
finally:
    module.close()
assert not level.alive
