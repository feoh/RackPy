# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack output=graph_expanders.out
from rackpy import laboratory
from rackpy.dsp.graph import Graph
from rackpy.dsp.graph_reference import run_pair
from rackpy.engine import Module
from rackpy.engine.expanders import graph_messages
from rackpy.laboratory import Bench

# A module that sends its knob's value to the module on its right...
send = Graph()
send.expander_send(1, send.param(0))

# ...and an expander that outputs what arrives from its left, one frame later.
receive = Graph()
receive.output(0, receive.expander_receive(0))

host = Module.create(lambda m: m.config(1, 0, 0, 0))
host.set_kernel(send.compile())
expander = Module.create(lambda m: m.config(0, 0, 1, 0))
graph_messages(expander.left_expander)  # a module receives in its own messages
expander.set_kernel(receive.compile())

# In a patch, Rack's engine runs both kernels and swaps the messages. Here a
# laboratory bench steps them the same way, offline (trusted code only).
laboratory.enable()
try:
    bench = Bench(48000)
    bench.add(host)
    bench.add(expander)
    bench.set_expander(host, 1, expander)  # Rack does this for adjacent modules
    bench.set_expander(expander, 0, host)
    for v in (1.0, 2.0, 3.0):
        host.params[0].set_value(v)
        bench.step()
        print(expander.outputs[0].get_voltage())  # 0.0, then 1.0, then 2.0
    bench.clear()
finally:
    laboratory.disable()

# The offline reference agrees, frame for frame.
_, b = run_pair(send, receive, 3, left_inputs={"params": {0: [1.0, 2.0, 3.0]}})
assert b.outputs[0] == [[0.0], [1.0], [2.0]]
