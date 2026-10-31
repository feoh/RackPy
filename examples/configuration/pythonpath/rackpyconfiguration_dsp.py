"""The configuration example's processing, as graphs: what Rack's engine runs, natively.

This module does not import Rack, so its graphs can be evaluated offline anywhere
(``rackpy.dsp.graph_reference.run``); the plugin compiles them into native kernels.
"""

from rackpy.dsp.graph import Graph

FREQ_C4 = 261.6256  # rack::dsp::FREQ_C4, the frequency pitch_to_freq gives 0 V

# The Shaper's ports and controls, by index; shaper.py names the same numbers.
LEVEL, SCALE, OFFSET, DEPTH, RATE, MODE, MUTE = range(7)
SIGNAL = 0
SHAPED = 0
LFO = 0

# The Splitter's.
SPLIT_LEVEL = 0
SPLIT_SIGNAL = 0
THROUGH, INVERTED = 0, 1


def shaper_graph() -> Graph:
    """Shape, scale and offset the signal, with a tremolo; a held MUTE button silences it.

    MODE chooses the shape: 0 passes the signal, 1 rectifies it, 2 keeps its positive half.
    LEVEL is a gain (shown in dB), SCALE an attenuverter, DEPTH the tremolo's depth (shown
    as a percentage) and RATE its frequency in octaves from 1 Hz (shown in Hz).
    """
    g = Graph()
    x = g.input(SIGNAL)
    mode = g.param(MODE)
    shaped = g.where(mode > 1.5, g.maximum(x, 0.0), g.where(mode > 0.5, abs(x), x))
    lfo = 0.5 + 0.5 * g.sin2pi(g.phasor(g.pitch_to_freq(g.param(RATE)) * (1.0 / FREQ_C4)))
    tremolo = 1.0 - g.param(DEPTH) * lfo
    out = shaped * g.param(LEVEL) * g.param(SCALE) * tremolo + g.param(OFFSET)
    g.output(SHAPED, g.where(g.param(MUTE) > 0.5, 0.0, out))
    g.light(LFO, lfo)
    return g


def splitter_graph() -> Graph:
    """The signal at LEVEL on THROUGH, and inverted on INVERTED."""
    g = Graph()
    x = g.input(SPLIT_SIGNAL) * g.param(SPLIT_LEVEL)
    g.output(THROUGH, x)
    g.output(INVERTED, -x)
    return g


def splitter_bypass_graph() -> Graph:
    """The Splitter, bypassed: unity gain on both outputs, the second still inverted.

    Rack's bypass routes can only copy an input to an output, so they cannot
    keep INVERTED inverted; a custom bypass (C++'s processBypass) can.
    """
    g = Graph()
    x = g.input(SPLIT_SIGNAL)
    g.output(THROUGH, x)
    g.output(INVERTED, -x)
    return g
