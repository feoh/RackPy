"""The Source's and the Receiver's processing, as graphs. This module does not import Rack.

The Source sends two lanes to its right neighbour: its scaled signal (lane 0) and its
LEVEL knob (lane 1). The Receiver reads them from its left one frame later. The plugin
compiles these graphs into native kernels; rackpy.dsp.graph_reference.run_pair evaluates
the pair offline, one frame apart, as Rack's engine runs them.
"""

from rackpy.dsp.graph import Graph

LEVEL = 0  # the Source's param
SIGNAL = 0  # its input
SCALED = 0  # its output
SIGNAL_LANE, LEVEL_LANE = 0, 1
LANES = 2
RECEIVED = 0  # the Receiver's output
LINK = 0  # its light
LEFT, RIGHT = 0, 1


def source_graph() -> Graph:
    g = Graph()
    level = g.param(LEVEL)
    scaled = g.input(SIGNAL) * level
    g.output(SCALED, scaled)
    g.expander_send(RIGHT, scaled, SIGNAL_LANE)
    g.expander_send(RIGHT, level, LEVEL_LANE)
    return g


def receiver_graph() -> Graph:
    g = Graph()
    g.output(RECEIVED, g.expander_receive(LEFT, SIGNAL_LANE))
    # Lit as brightly as the Source's LEVEL; with no Source on the left nothing arrives: 0 channels, dark.
    g.light(LINK, g.expander_receive(LEFT, LEVEL_LANE))
    return g
