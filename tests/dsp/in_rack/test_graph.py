# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-21B: graph plans inside Rack, run by the native plan interpreter (live UI suite).

A Python-owned module is stepped frame by frame on the main thread
(``_testing.module_process``) and every output, channel count and light is
compared with the offline reference: exactly for arithmetic, comparisons,
phasors, triggers, pulses, delays and controls, and within a tolerance where
``sin2pi`` and ``pitch_to_freq`` round differently from Python's functions.
Then plans run in Rack's own engine: controls through the queue, wiring
checks, replacement under load and teardown.
"""

import gc
import math
import random
import time
from collections.abc import Callable

import pytest
from _rackpy_host import _testing as HOOKS
from _rackpy_host import dsp as native

from rackpy.dsp.graph import Graph, GraphError
from rackpy.dsp.graph_reference import f32, run
from rackpy.dsp.kernel import Kernel
from rackpy.errors import StaleHandleError
from rackpy.raw.engine import Module


def until(condition: Callable[[], bool], seconds: float = 2.0) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.005)
    return condition()


def adopted(configure: Callable[[Module], object]) -> Module:
    m = Module.create(configure)
    HOOKS.engine_adopt(m)
    return m


def everything() -> Graph:
    """Every node type with exact float32 semantics (no sin2pi or pitch_to_freq), on up to 16 channels."""
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


type Stepped = tuple[dict[int, list[list[float]]], list[float]]


def step(
    m: Module, frames: int, rates: list[float], inputs: dict[int, list[list[float]]], params: list[float]
) -> Stepped:
    """Steps frames 0.. of the given inputs, params and rates; each output's channels per frame, and light 0."""
    outs: dict[int, list[list[float]]] = {p: [] for p in range(len(m.outputs))}
    lights: list[float] = []
    for frame in range(frames):
        for port, values in inputs.items():
            m.inputs[port].channels = len(values[frame])
            for c, v in enumerate(values[frame]):
                m.inputs[port].set_voltage(v, c)
        m.params[0].value = params[frame]
        HOOKS.module_process(m, rates[frame], frame)
        for port, frames_out in outs.items():
            o = m.outputs[port]
            frames_out.append([o.get_voltage(c) for c in range(o.get_channels())])
        if len(m.lights):
            lights.append(m.lights[0].get_brightness())
    return outs, lights


def same(a: float, b: float) -> bool:
    return (math.isnan(a) and math.isnan(b)) or a == b


def test_a_compiled_graph_is_a_kernel() -> None:
    g = everything()
    k = g.compile()
    assert isinstance(k, Kernel) and not k.attached and k.alive
    assert k.type.name == "rackpy.Graph" and k.type.controls == ("level",)
    assert k.values == {
        "input[0]": 0,
        "input[1]": 1,
        "param[0]": 0,
        "output[0]": 0,
        "output[1]": 1,
        "output[2]": 2,
        "output[3]": 3,
        "output[4]": 4,
        "output[5]": 5,
        "light[0]": 0,
        "level": 0.5,
    }
    k.set("level", 2)
    assert k.values["level"] == 2.0 and repr(k) == "<kernel rackpy.Graph (not attached)>"
    with pytest.raises(KeyError, match="no control volume"):
        k.set("volume", 1)
    assert g.compile().type is not k.type  # each plan has its own type


def test_plans_replay_the_offline_reference_exactly() -> None:
    frames = 600
    rng = random.Random(21)
    inputs = {
        port: [[f32(rng.uniform(-3, 3)) for _ in range(rng.choice([0, 1, 1, 3, 16]))] for _ in range(frames)]
        for port in (0, 1)
    }
    params = [f32(rng.uniform(-1, 1)) for _ in range(frames)]
    rates = [48000.0] * 300 + [96000.0] * 300
    g = everything()
    m = Module.create(lambda m: m.config(1, 2, 6, 1))
    try:
        for o in m.outputs:
            o.channels = 1  # patched, as Rack marks a connected output
        kernel = g.compile()
        m.set_kernel(kernel)
        first_outs, first_lights = step(m, 200, rates, inputs, params)
        kernel.set("level", -1.75)  # a Python-owned module applies it at once
        later = {port: values[200:] for port, values in inputs.items()}
        rest_outs, rest_lights = step(m, frames - 200, rates[200:], later, params[200:])
    finally:
        m.close()
    expected = run(
        g,
        frames,
        sample_rate=48000,
        rate_changes={300: 96000},
        inputs=inputs,
        params={0: params},
        controls={200: {"level": -1.75}},
    )
    for port in range(6):
        got = first_outs[port] + rest_outs[port]
        want = expected.outputs[port]
        for frame, (a, b) in enumerate(zip(got, want, strict=True)):
            assert len(a) == len(b) and all(same(x, y) for x, y in zip(a, b, strict=True)), (port, frame, a, b)
    assert first_lights + rest_lights == expected.lights[0]


def test_the_tutorial_oscillator_runs_within_rounding_of_the_reference() -> None:
    g = Graph()
    pitch = g.param(0) + g.input(0)
    g.output(0, 5 * g.sin2pi(g.phasor(g.pitch_to_freq(pitch))))
    g.light(0, g.phasor(1.0) < 0.5)
    frames = 2000
    inputs = {0: [[f32(0.5), f32(-1.0), f32(0.25)]] * frames}
    m = Module.create(lambda m: m.config(1, 1, 1, 1))
    try:
        m.outputs[0].channels = 1
        m.set_kernel(g.compile())
        outs, lights = step(m, frames, [44100.0] * frames, inputs, [f32(0.25)] * frames)
    finally:
        m.close()
    expected = run(g, frames, sample_rate=44100, inputs=inputs, params={0: f32(0.25)})
    pairs = zip(outs[0], expected.outputs[0], strict=True)
    worst = max(abs(a - b) for fa, fb in pairs for a, b in zip(fa, fb, strict=True))
    assert worst < 1e-3  # libm's float sin and pow against Python's, rounded: the phases drift apart slowly
    assert lights == expected.lights[0]


def test_wiring_and_bad_plans_are_refused() -> None:
    g = Graph()
    g.output(0, g.input(3) * 2)
    m = Module.create(lambda m: m.config(0, 1, 1, 0))
    try:
        with pytest.raises(ValueError, match=r"input\[3\] = 3, but the module has 1 inputs"):
            m.set_kernel(g.compile())
        assert m.kernel is None
    finally:
        m.close()
    looped = Graph()
    f = looped.forward()
    f.bind(f + 1)
    looped.output(0, f)
    with pytest.raises(GraphError, match="cycle with no delay"):
        looped.compile()
    plan = everything().plan().to_dict()
    for broken, error, message in (
        ({**plan, "nodes": [{"op": "fft", "inputs": [], "args": []}]}, ValueError, "not a node type"),
        ({**plan, "latches": []}, ValueError, "never fed"),
        ({k: v for k, v in plan.items() if k != "controls"}, TypeError, 'needs "controls"'),
        ({**plan, "nodes": [{"op": "output", "inputs": [True], "args": [0]}]}, TypeError, "must be ints"),
        ({**plan, "nodes": [{"op": "output", "inputs": [3], "args": [0]}]}, ValueError, "only earlier nodes"),
    ):
        with pytest.raises(error, match=message):
            native.graph_kernel(broken)


def test_rack_runs_a_plan_and_its_controls() -> None:
    g = Graph()
    g.output(0, g.input(0) * g.control("gain", 2.0))
    source = adopted(lambda m: m.config(0, 0, 1, 0))

    def configure(m: Module) -> None:
        m.config(0, 1, 1, 0)
        m.set_kernel(g.compile())

    vca = adopted(configure)
    cable = HOOKS.engine_connect(source, 0, vca, 0)
    try:
        source.outputs[0].set_voltage(1.5)
        out = vca.outputs[0]
        assert until(lambda: out.get_voltage() == 3.0)
        kernel = vca.kernel
        assert kernel is not None and kernel.type.name == "rackpy.Graph"
        kernel.set("gain", -4)  # through the bounded queue, at a frame boundary
        assert until(lambda: out.get_voltage() == -6.0)
    finally:
        HOOKS.engine_disconnect(cable)
        HOOKS.engine_delete(vca, None)
        HOOKS.engine_delete(source, None)


def test_plans_are_replaced_under_load_and_torn_down() -> None:
    m = adopted(lambda m: m.config(0, 0, 1, 0))
    kernels: list[Kernel] = []
    try:
        for i in range(300):
            g = Graph()
            count = g.delay()
            count.feed(count + 1)
            g.output(0, count * 0 + i)
            k = g.compile()
            m.set_kernel(k)
            kernels.append(k)
            if i % 50 == 0:
                time.sleep(0.002)
        last = kernels[-1]
        assert m.kernel is last and last.alive and not kernels[0].alive
        assert until(lambda: m.outputs[0].get_voltage() == 299.0)
        with pytest.raises(StaleHandleError, match="replaced"):
            kernels[-2].values  # noqa: B018 - the property raises
        m.set_kernel(single(5.0).compile())  # still waiting when the module goes away
    finally:
        HOOKS.engine_delete(m, None)
    gc.collect()
    assert not last.alive


def single(value: float) -> Graph:
    g = Graph()
    g.output(0, g.constant(value))
    return g
