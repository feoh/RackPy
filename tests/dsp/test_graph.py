# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-21A: the graph model, its validation and the offline reference.

Pure Python: graphs are built, validated and lowered, and the reference's
results are checked against values worked out by hand from Rack's code (the
tutorial oscillator, dsp::SchmittTrigger, dsp::PulseGenerator,
Port::setChannels). The native tiers are RP-21B and RP-21C.
"""

import math
from pathlib import Path

import pytest

from rackpy.dsp import graph_reference
from rackpy.dsp.graph import OPS, Graph, GraphError, Plan, Signal
from rackpy.dsp.graph_reference import f32, run, run_pair

pytestmark = pytest.mark.unit
HERE = Path(__file__).name


def tutorial() -> Graph:
    """Rack's tutorial oscillator: a sine at the pitch knob plus the V/oct input, and a 1 Hz blinking light."""
    g = Graph()
    pitch = g.param(0) + g.input(0)
    g.output(0, 5 * g.sin2pi(g.phasor(g.pitch_to_freq(pitch))))
    g.light(0, g.phasor(1.0) < 0.5)
    return g


def test_the_tutorial_oscillator_follows_rack() -> None:
    frames, rate = 200, 44100.0
    result = run(tutorial(), frames, sample_rate=rate, params={0: 0.25}, inputs={0: [[0.5]] * frames})
    # Rack's tutorial, in float32: pitch = param + input; phase += freq * sampleTime; less 1 at 1; 5 sin(2 pi phase).
    dt, phase, blink = f32(1 / f32(rate)), 0.0, 0.0
    for frame in range(frames):
        freq = f32(graph_reference.FREQ_C4 * f32(2 ** f32(0.25 + 0.5)))
        phase = f32(phase + f32(freq * dt))
        if phase >= 1:
            phase = f32(phase - 1)
        assert result.outputs[0][frame] == [f32(5 * f32(math.sin(f32(f32(2 * f32(math.pi)) * phase))))]
        blink = f32(blink + dt)
        if blink >= 1:
            blink = f32(blink - 1)
        assert result.lights[0][frame] == (1.0 if blink < 0.5 else 0.0)


def test_a_light_blinks_at_one_hertz() -> None:
    g = Graph()
    g.light(0, g.phasor(1.0) < 0.5)
    result = run(g, 8, sample_rate=4.0)
    assert result.lights[0] == [1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0]  # phases 0.25, 0.5, 0.75, 0 (1 less 1), ...


def test_a_vca_and_polyphony() -> None:
    g = Graph()
    g.output(0, g.input(0) * g.param(0))
    g.output(1, g.input(0) + g.input(1))
    stereo = [[1.0, -2.0, 3.0]]
    result = run(g, 1, inputs={0: stereo, 1: [[10.0]]}, params={0: 0.5})
    assert result.outputs[0] == [[0.5, -1.0, 1.5]]
    assert result.outputs[1] == [[11.0, 8.0, 13.0]]  # a mono operand broadcasts
    result = run(g, 1, inputs={0: stereo, 1: [[10.0, 20.0]]})
    assert result.outputs[1] == [[11.0, 18.0, 3.0]]  # missing channels of a shorter one read 0 V
    result = run(g, 1, params={0: 0.5})
    assert result.outputs == {0: [[0.0]], 1: [[0.0]]}  # unpatched inputs have no channels; outputs have at least one


def test_channel_counts_follow_the_inputs_each_frame() -> None:
    g = Graph()
    g.output(0, g.input(0) * 2)
    result = run(g, 3, inputs={0: [[1.0], [1.0] * 16, []]})
    assert [len(frame) for frame in result.outputs[0]] == [1, 16, 1]


def test_feedback_through_a_delay() -> None:
    g = Graph()
    count = g.delay()
    count.feed(count + 1)
    g.output(0, count)
    assert run(g, 4).outputs[0] == [[0.0], [1.0], [2.0], [3.0]]
    # A one-pole lowpass, y = y[-1] + (x - y[-1]) / 2, fed through a forward reference.
    g = Graph()
    y = g.delay(initial=8.0)
    smoothed = g.forward()
    g.output(0, smoothed)
    smoothed.bind(y + (g.input(0) - y) * 0.5)
    y.feed(smoothed)
    assert run(g, 3, inputs={0: [[0.0]] * 3}).outputs[0] == [[4.0], [2.0], [1.0]]


def test_a_delay_keeps_the_channels_it_was_fed() -> None:
    g = Graph()
    d = g.delay(initial=7.0)
    d.feed(g.input(0))
    g.output(0, d)
    result = run(g, 3, inputs={0: [[1.0, 2.0, 3.0], [4.0], [5.0]]})
    assert result.outputs[0] == [[7.0], [1.0, 2.0, 3.0], [4.0]]


def test_a_cycle_without_a_delay_is_refused_with_its_path() -> None:
    g = Graph()
    loop = g.forward()
    louder = loop * 2
    loop.bind(louder + g.input(0))
    g.output(0, loop)
    with pytest.raises(GraphError, match="cycle with no delay") as raised:
        g.plan()
    message = str(raised.value)
    assert "add" in message and "mul" in message and HERE in message and "Graph.delay()" in message


def test_triggers_and_pulses_follow_rack() -> None:
    g = Graph()
    edge = g.schmitt(g.input(0), 0.1, 2.0)
    g.output(0, edge)
    g.output(1, g.pulse(edge, 0.5))
    gate = [[2.0], [0.0], [2.0], [1.0], [2.0], [0.05], [3.0], [0.0]]
    result = run(g, len(gate), sample_rate=4.0, inputs={0: gate})
    # UNINITIALIZED goes HIGH at first without firing; then it fires on each rise from at most 0.1 to 2.
    assert [f[0] for f in result.outputs[0]] == [0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0]
    # 0.5 s at 4 Hz: high for two frames from each trigger.
    assert [f[0] for f in result.outputs[1]] == [0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0]


def test_controls_and_sample_rate_changes_apply_from_their_frame() -> None:
    g = Graph()
    g.output(0, g.control("gain", 1.0) * 3)
    g.output(1, g.sample_rate())
    g.light(0, g.sample_time())
    result = run(g, 4, sample_rate=48000, controls={2: {"gain": 0.5}}, rate_changes={3: 96000})
    assert [f[0] for f in result.outputs[0]] == [3.0, 3.0, 1.5, 1.5]
    assert [f[0] for f in result.outputs[1]] == [48000.0, 48000.0, 48000.0, 96000.0]
    assert result.lights[0][3] == f32(1 / 96000)
    with pytest.raises(GraphError, match="unknown controls"):
        run(g, 4, controls={1: {"volume": 1.0}})


def test_functions_follow_ieee_and_rack() -> None:
    g = Graph()
    x, y = g.input(0), g.input(1)
    for port, signal in enumerate(
        [x / y, g.minimum(x, y), g.maximum(x, y), x.clamp(-1, 1), g.where(x, y, -y), abs(x), -x, x >= y, x <= y, x > y]
    ):
        g.output(port, signal)
    inputs = {0: [[1.0, -1.0, 0.0, math.nan, 5.0]], 1: [[0.0, 0.0, 0.0, 2.0, -0.0]]}
    out = run(g, 1, inputs=inputs).outputs
    lanes = [frame[0] for frame in (out[p] for p in range(10))]
    assert lanes[0][:2] == [math.inf, -math.inf] and math.isnan(lanes[0][2]) and math.isnan(lanes[0][3])
    assert lanes[0][4] == -math.inf
    assert lanes[1][3] == 2.0 and lanes[2][3] == 2.0  # fmin and fmax ignore a NaN
    assert lanes[3] == [1.0, -1.0, 0.0, 1.0, 1.0]  # clamp(NaN) is fmax(fmin(NaN, 1), -1) = 1
    assert lanes[4] == [0.0, -0.0, -0.0, -2.0, -0.0]
    assert lanes[5][:3] == [1.0, 1.0, 0.0] and lanes[6][:3] == [-1.0, 1.0, -0.0]
    assert lanes[7] == [1.0, 0.0, 1.0, 0.0, 1.0] and lanes[8] == [0.0, 1.0, 1.0, 0.0, 0.0] and lanes[9][4] == 1.0


def test_the_plan_is_ordered_pruned_and_plain_data() -> None:
    g = Graph()
    unused = g.input(5) * 2
    gain = g.control("gain", 0.5)
    d = g.delay()
    d.feed(g.input(0) * gain)
    g.output(0, d + 1)
    g.control("never_used")
    assert isinstance(unused, Signal)
    plan = g.plan()
    assert isinstance(plan, Plan)
    ops = [n.op for n in plan.nodes]
    assert "input" in ops and ops.count("input") == 1  # the unused input is pruned
    for k, node in enumerate(plan.nodes):
        assert all(i < k for i in node.inputs), "each node reads only earlier ones"
        assert len(node.inputs) == OPS[node.op].inputs and len(node.args) == len(OPS[node.op].args)
    [(delay, source)] = plan.latches
    assert plan.nodes[delay].op == "delay" and plan.nodes[source].op == "mul"
    assert [c.name for c in plan.controls] == ["gain"] and plan.nodes[plan.controls[0].node].op == "control"
    data = plan.to_dict()
    assert data["latches"] == [[delay, source]] and data["controls"] == [
        {"name": "gain", "node": plan.controls[0].node, "default": 0.5}
    ]


def test_refusals_say_what_to_change() -> None:
    g = Graph()
    s = g.input(0)
    g.output(0, s)
    with pytest.raises(GraphError, match="output port 0 is already written"):
        g.output(0, s)
    g.light(1, s)
    with pytest.raises(GraphError, match="light 1 is already set"):
        g.light(1, s)
    with pytest.raises(GraphError, match="another graph"):
        Graph().output(0, s)
    with pytest.raises(GraphError, match="not compiled"):
        g.output(2, "loud")  # type: ignore[arg-type]
    with pytest.raises(GraphError, match="not compiled"):
        g.output(2, math.sin)  # type: ignore[arg-type]
    with pytest.raises(TypeError, match="cannot branch"):
        if s:
            pass
    with pytest.raises(TypeError, match="not compiled"):
        math.sin(s)
    for bad, match in ((True, "whole number"), (-1, "not be negative"), (1.5, "whole number")):
        with pytest.raises(GraphError, match=match):
            g.input(bad)  # type: ignore[arg-type]
    with pytest.raises(GraphError, match="finite"):
        g.constant(math.nan)
    with pytest.raises(GraphError, match="identifier"):
        g.control("two words")
    g.control("gain")
    with pytest.raises(GraphError, match="already defined"):
        g.control("gain")
    with pytest.raises(GraphError, match="not be negative"):
        g.pulse(s, -1.0)
    d = g.delay()
    d.feed(s)
    with pytest.raises(GraphError, match="already fed"):
        d.feed(s)
    with pytest.raises(GraphError, match="no output, light or expander lane"):
        Graph().plan()
    unfed = Graph()
    unfed.output(0, unfed.delay())
    with pytest.raises(GraphError, match=f"never fed.*|{HERE}"):
        unfed.plan()
    unbound = Graph()
    unbound.output(0, unbound.forward())
    with pytest.raises(GraphError, match="never bound"):
        unbound.plan()
    circular = Graph()
    a, b = circular.forward(), circular.forward()
    a.bind(b)
    b.bind(a)
    circular.output(0, a)
    with pytest.raises(GraphError, match="in a loop"):
        circular.plan()


def test_the_reference_checks_its_inputs() -> None:
    with pytest.raises(ValueError, match="2 frames"):
        run(tutorial(), 3, inputs={0: [[0.0], [0.0]]})
    with pytest.raises(ValueError, match="more than 16"):
        run(tutorial(), 1, inputs={0: [[0.0] * 17]})
    with pytest.raises(ValueError, match="1 values"):
        run(tutorial(), 2, params={0: [0.0]})


def test_compiling_needs_rack() -> None:
    g = Graph()
    g.output(0, g.input(0))
    with pytest.raises(ImportError, match=r"needs Rack.*graph_reference"):
        g.compile()


def test_native_extension_nodes_lower_by_name() -> None:
    g = Graph()
    folded = g.node("example.Fold", g.input(0), drive=2.0, limit=5)
    g.output(0, folded + 1)
    plan = g.plan()
    [node] = [n for n in plan.nodes if n.op == "example.Fold"]
    assert node.args == (2.0, 5.0) and node.names == ("drive", "limit") and len(node.inputs) == 1
    assert plan.to_dict()["nodes"][plan.nodes.index(node)]["names"] == ["drive", "limit"]
    assert "names" not in plan.to_dict()["nodes"][0]  # RackPy's own node types carry none
    with pytest.raises(GraphError, match=r"cannot run the native node type example\.Fold"):
        run(g, 1)
    for bad in ("Fold", "a.b.c", "example.not valid", 3):
        with pytest.raises(GraphError, match=r"namespace\.Name"):
            g.node(bad)  # type: ignore[arg-type]
    with pytest.raises(GraphError, match="at most 8"):
        g.node("example.Mix", *[g.input(0)] * 9)
    with pytest.raises(GraphError, match="finite"):
        g.node("example.Fold", g.input(0), drive=math.inf)


# --- Expanders (RP-23B) ---------------------------------------------------------------------


def counter(g: Graph, start: float = 1.0) -> Signal:
    """Frame n: n + start."""
    count = g.delay(start)
    count.feed(count + 1)
    return count


def test_expander_nodes_lower_and_are_checked() -> None:
    g = Graph()
    g.expander_send(1, g.input(0), lane=2)
    g.output(0, g.expander_receive(0))
    plan = g.plan()
    ops = {n.op: n for n in plan.nodes}
    assert ops["expander_send"].args == (1.0, 2.0) and len(ops["expander_send"].inputs) == 1
    assert ops["expander_receive"].args == (0.0, 0.0) and ops["expander_receive"].inputs == ()
    only_sends = Graph()
    only_sends.expander_send(0, 1.0)  # a sink of its own
    assert [n.op for n in only_sends.plan().nodes] == ["constant", "expander_send"]
    for side, lane, match in ((2, 0, "side is 0"), (True, 0, "side is 0"), (0, 64, "0 to 63"), (0, -1, "negative")):
        with pytest.raises(GraphError, match=match):
            g.expander_send(side, 1.0, lane)
        with pytest.raises(GraphError, match=match):
            g.expander_receive(side, lane)
    with pytest.raises(GraphError, match="lane 2 of side 1 is already sent"):
        g.expander_send(1, 0.0, lane=2)
    with pytest.raises(GraphError, match="lane 0 of side 0 is already received"):
        g.expander_receive(0, 0)
    for op in ("expander_send", "expander_receive"):
        assert OPS[op].args == ("side", "lane")


def test_the_reference_receives_and_records_lanes() -> None:
    g = Graph()
    g.output(0, g.expander_receive(0) * 2)
    g.expander_send(1, g.input(0), lane=5)
    result = run(g, 3, inputs={0: [[1.0, 2.0], [], [3.0]]}, received={(0, 0): [[], [0.5, 1.5], [4.0]]})
    assert result.outputs[0] == [[0.0], [1.0, 3.0], [8.0]]  # nothing received: 0 channels (an output shows 0 V)
    assert result.sent == {(1, 5): [[1.0, 2.0], [], [3.0]]}
    with pytest.raises(ValueError, match="receives 1 frames"):
        run(g, 3, received={(0, 0): [[]]})


def test_a_pair_exchanges_one_frame_apart_both_ways() -> None:
    left, right = Graph(), Graph()
    left.expander_send(1, counter(left))  # 1, 2, 3, ...
    left.output(0, left.expander_receive(1, lane=3))
    got = right.expander_receive(0)
    right.output(0, got)
    right.output(1, counter(right))
    right.expander_send(0, got + 100, lane=3)  # echoes what it got
    a, b = run_pair(left, right, 6)
    assert b.outputs[1] == [[float(n + 1)] for n in range(6)]
    assert b.outputs[0] == [[0.0]] + [[float(n)] for n in range(1, 6)]  # left's counter of frame n - 1
    # The echo right sent at n - 1 (what it got then, 0 V at first, plus 100) reaches left at n.
    assert a.outputs[0] == [[0.0]] + [[float(n - 1) + 100] for n in range(1, 6)]
    with pytest.raises(TypeError, match="left_inputs takes"):
        run_pair(left, right, 1, left_inputs={"received": {}})


def test_a_pair_only_connects_the_facing_sides() -> None:
    left, right = Graph(), Graph()
    left.expander_send(0, 1.0)  # its own left: nobody there
    left.expander_send(1, 2.0)
    right.output(0, right.expander_receive(1))  # its own right: nobody there
    right.output(1, right.expander_receive(0))
    _, b = run_pair(left, right, 2)
    assert b.outputs[0] == [[0.0], [0.0]] and b.outputs[1] == [[0.0], [2.0]]
