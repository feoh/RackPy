# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy import laboratory
from rackpy.engine import Module
from rackpy.engine.expanders import ExpanderMessage, allocate_messages
from rackpy.laboratory import Bench

# A RackPy module allocates both buffers of a side, as a Rack module does.
mixer = Module.create(lambda m: m.config(0, 0, 1, 0))
producer, consumer = allocate_messages(mixer.right_expander, "demo.Levels", 4)
assert isinstance(mixer.right_expander.producer_message, ExpanderMessage)
producer.write([1.0, 0.5, 0.25, 0.0])  # allowed: Rack's engine does not run this module

laboratory.enable()  # trusted code only; a bench swaps the buffers as Rack's engine does
try:
    neighbour = Module.create(lambda m: m.config(0, 0, 1, 0))
    bench = Bench(48000)
    bench.add(mixer)
    bench.add(neighbour)
    bench.set_expander(mixer, 1, neighbour)  # what Rack does for adjacent modules
    mixer.right_expander.request_message_flip()
    bench.step()  # the swap happens at the end of the frame
    assert mixer.right_expander.module is neighbour
    assert mixer.right_expander.consumer_message is producer
    assert consumer.owner is mixer  # the module keeps both buffers alive
    bench.clear()
finally:
    laboratory.disable()
