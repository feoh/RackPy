# SPDX-License-Identifier: GPL-3.0-or-later
"""The oscillator's DSP, described in Python; it does not import Rack, so it also runs offline.

``oscillator_graph()`` is what the module runs in Rack: ``Graph.compile()`` turns it into a
native kernel and Python never runs per sample. ``offline_reference()`` evaluates the same
graph in pure Python, for teaching and tests only; Rack never calls it.
"""

from collections.abc import Sequence

from rackpy.dsp.graph import Graph
from rackpy.dsp.graph_reference import Result, run

AMPLITUDE = 5.0
"""Peak output voltage: Rack's audio-rate signals are +-5 V."""

BLINK_HZ = 1.0
"""The light's blink rate; it is lit for the first half of each period."""


def oscillator_graph(pitch_param: int = 0, pitch_input: int = 0, sine_output: int = 0, blink_light: int = 0) -> Graph:
    """A sine at the pitch knob plus the 1 V/octave input (0 V is middle C), and a 1 Hz blink.

    The knob is mono; the input may be polyphonic, and the output then has its channels.
    """
    g = Graph()
    pitch = g.param(pitch_param) + g.input(pitch_input)
    g.output(sine_output, AMPLITUDE * g.sin2pi(g.phasor(g.pitch_to_freq(pitch))))
    g.light(blink_light, g.phasor(BLINK_HZ) < 0.5)
    return g


def offline_reference(
    frames: int, *, sample_rate: float, knob: float | Sequence[float] = 0.0, voct: Sequence[float] | None = None
) -> Result:
    """Offline reference, pure Python: the graph's output and light for ``frames`` frames.

    ``knob`` is the pitch knob in volts, constant or one value per frame; ``voct`` is the
    mono input's voltage per frame, or None for an unpatched input.
    """
    inputs = None if voct is None else {0: [[v] for v in voct]}
    return run(oscillator_graph(), frames, sample_rate=sample_rate, params={0: knob}, inputs=inputs)
