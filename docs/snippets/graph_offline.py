# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=python output=graph_offline.out
from rackpy.dsp.graph import Graph
from rackpy.dsp.graph_reference import run

g = Graph()
# A VCA: input 0 times parameter 0, on output 0, for every channel of the cable.
g.output(0, g.input(0) * g.param(0))
# A light that blinks at 1 Hz: a phase from 0 to 1, compared with 0.5.
g.light(0, g.phasor(1.0) < 0.5)
# Feedback needs an explicit one-sample delay: a counter on output 1.
count = g.delay()
count.feed(count + 1)
g.output(1, count)

plan = g.plan()
print([node.op for node in plan.nodes])

# The offline reference: 4 frames at a sample rate of 4 Hz, with a 2-channel cable on input 0.
result = run(g, 4, sample_rate=4.0, inputs={0: [[1.0, -2.0]] * 4}, params={0: 0.5})
print(result.outputs[0])
print(result.lights[0])
print(result.outputs[1])
