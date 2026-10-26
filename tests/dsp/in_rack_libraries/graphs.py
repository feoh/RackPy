# SPDX-License-Identifier: GPL-3.0-or-later
"""Graphs for the compiled tier's tests (RP-21C).

``bin/rackpy-graph emit tests/dsp/in_rack_libraries/graphs.py graphs`` writes
tests/dsp/kernel_library/graphs.cpp from GRAPHS (tests/dsp/test_graph.py
checks that it is current); the hooks build compiles it as the kernel
library ``graphs``. test_libraries.py runs each compiled kernel against the
plan interpreter inside Rack, and native/tests/graph_bench.cpp times them.
CONFIG gives each graph's module: (params, inputs, outputs, lights).
"""

from rackpy.dsp.graph import Graph


def tutorial() -> Graph:
    """Rack's tutorial oscillator, with its level as a control, and a light blinking at 1 Hz."""
    g = Graph()
    pitch = g.param(0) + g.input(0)
    g.output(0, g.control("level", 5.0) * g.sin2pi(g.phasor(g.pitch_to_freq(pitch))))
    g.light(0, g.phasor(1.0) < 0.5)
    return g


def everything() -> Graph:
    """Every node type with exact float32 semantics, on up to 16 channels."""
    g = Graph()
    x, y, p = g.input(0), g.input(1), g.param(0)
    level = g.control("level", 0.5)
    g.output(0, (x * level + y - p) / (abs(y) + 1))
    g.output(1, g.where(x > y, g.minimum(x, p), g.maximum(-y, p)).clamp(-2, 3))
    g.output(2, (x < y) + (x <= p) * 2 + (y >= p) * 4)
    edge = g.schmitt(x, 0.1, 2.0)
    g.output(3, edge + g.pulse(edge, 0.003) * 2)
    smooth = g.delay(1.0)
    smooth.feed(smooth + (x - smooth) * 0.25)
    g.output(4, smooth)
    g.output(5, g.phasor(g.constant(440.0) + p * 100) + g.sample_rate() / 1e6 + g.sample_time())
    g.light(0, g.phasor(1.5) < 0.5)
    return g


def sine(gain: bool) -> Graph:
    """RP-07's reference sine (5 V at the V/oct input 0), times param 0 if `gain`: the benchmark's graphs."""
    g = Graph()
    out = 5 * g.sin2pi(g.phasor(g.pitch_to_freq(g.input(0))))
    g.output(0, out * g.param(0) if gain else out)
    return g


def vca() -> Graph:
    g = Graph()
    g.output(0, g.input(0) * g.param(0))
    return g


GRAPHS = {"Tutorial": tutorial(), "Everything": everything(), "Sine": sine(False), "SineGain": sine(True), "Vca": vca()}
CONFIG = {
    "Tutorial": (1, 1, 1, 1),
    "Everything": (1, 2, 6, 1),
    "Sine": (0, 1, 1, 0),
    "SineGain": (1, 1, 1, 0),
    "Vca": (1, 1, 1, 0),
}
