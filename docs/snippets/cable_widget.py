# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy import raw
from rackpy.raw import app
from rackpy.raw import history as h


def connect(output_port: app.PortWidget, input_port: app.PortWidget) -> app.CableWidget:
    """Call from Rack's UI thread: a cable between two port widgets in the rack, with an undo step."""
    context = raw.context_get()
    assert context is not None and context.scene is not None and context.history is not None
    cable = app.CableWidget()
    cable.output_port, cable.input_port = output_port, input_port
    cable.color = context.scene.rack.get_next_cable_color()
    context.scene.rack.add_cable(cable)  # Rack's rack takes it, and its plugs
    cable.update_cable()  # its engine cable, between the two ports
    step = h.CableAdd()
    step.set_cable(cable)
    context.history.push(step)  # done, then pushed, as Rack's dragging does
    return cable
