# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.engine import Module, Port
from rackpy.errors import ContextError

# Rack's event records are plain values.
cable = Module.PortChangeEvent(connecting=True, type=Port.Type.INPUT, port_id=0)
assert cable.connecting and cable.port_id == 0

module = Module.create(lambda m: m.config(num_params=0, num_inputs=1, num_outputs=0))
try:
    assert module.lifecycle_events() == [] and module.dropped_lifecycle_events == 0
    try:
        # Rack's handler for this module, called directly as C++ plugin code may.
        module.on_port_change(cable)
    except ContextError:
        pass  # at plugin start-up, as this example runs, Rack has no engine yet
    else:
        [(sequence, event)] = module.lifecycle_events()
        assert sequence == 1 and isinstance(event, Module.PortChangeEvent) and event.connecting
finally:
    module.close()
