# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import ParamQuantity, SwitchQuantity
from rackpy.raw import Quantity
from rackpy.raw.engine import Module


class Captioned(ParamQuantity):
    def get_label(self) -> str:
        return super().get_label() + " (preview)"

    def get_description(self) -> str:
        return "An unattached quantity, not an engine parameter."


quantity = Captioned()
quantity.name = "Pitch"
quantity.unit = " V"
assert quantity.module is None and quantity.get_param() is None
quantity.set_value(0.75)  # Rack's null-module setter is a no-op, not value storage.
assert quantity.get_value() == 0

# A weak attachment gives Rack's getParam/value semantics without transferring
# this quantity into the module's engine-owned metadata slots.
module = Module.create(lambda m: m.config(1, 0, 0))
quantity.module = module
quantity.param_id = 0
assert quantity.module is module and quantity.get_param() is module.params[0]
# Attached value/display methods are only valid with a live bound Rack UI context.
quantity.module = None
module.close()
assert quantity.get_param() is None
assert Quantity.get_label(quantity) == ""  # explicitly Quantity's base implementation
assert quantity.get_string() == "Pitch (preview): 0 V"  # native getString dispatches to the label override
assert quantity.get_description().startswith("An unattached")

switch = SwitchQuantity()
switch.labels = ["Low", "High"]
labels = switch.labels
labels[0] = "Bass"
assert switch.get_display_value_string() == "Bass"
switch.labels = ["First", "Second"]
assert labels.tolist() == ["First", "Second"]
# An owned alias keeps its standalone quantity alive; borrowed module aliases do not.
del switch
labels.append("Third")
assert list(labels) == ["First", "Second", "Third"]
