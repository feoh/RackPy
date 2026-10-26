# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-17B: a plugin's own C++ kernel library, run by Rack's engine (live UI suite).

tests/dsp/test_runtime.py stages the example library and several broken ones;
only the example's types may appear. RP-21C: the example's graph node types
too, used from Python graphs, and the "graphs" library that bin/rackpy-graph
compiled from graphs.py, each kernel checked against the plan interpreter.
"""

import math
import random
import runpy
import time
from collections.abc import Callable
from pathlib import Path

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.dsp.graph import Graph, Signal, node_types
from rackpy.dsp.graph_reference import f32
from rackpy.dsp.kernel import kernel_types, load
from rackpy.raw.engine import Module

# The graphs bin/rackpy-graph compiled into the "graphs" kernel library (tests/dsp/kernel_library/graphs.cpp).
_GRAPHS = runpy.run_path(str(Path(__file__).with_name("graphs.py")))
GRAPHS: dict[str, Graph] = _GRAPHS["GRAPHS"]
CONFIG: dict[str, tuple[int, int, int, int]] = _GRAPHS["CONFIG"]


def until(condition: Callable[[], bool], seconds: float = 2.0) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.005)
    return condition()


def test_only_the_valid_library_registered_types() -> None:
    names = {t.name for t in kernel_types()}
    assert "example.Fold" in names
    for refused in ("wrongabi.Never", "throws.Partial", "badname.Good", "rackpy.Gain2"):
        assert refused not in names
    fold = load("example.Fold")
    assert fold.controls == ("gain", "limit") and [f[0] for f in fold.fields] == ["input", "output", "gain", "limit"]
    with pytest.raises(ValueError, match="limit must be positive"):
        fold(input=0, output=0, limit=0)  # the library's own exception, as ValueError


def test_rack_runs_the_library_kernel() -> None:
    fold = load("example.Fold")
    source = Module.create(lambda m: m.config(0, 0, 1, 0))

    def folder(m: Module) -> None:
        m.config(0, 1, 1, 0)
        m.set_kernel(fold(input=0, output=0, limit=5))

    m = Module.create(folder)
    HOOKS.engine_adopt(source)
    HOOKS.engine_adopt(m)
    cable = HOOKS.engine_connect(source, 0, m, 0)
    try:
        out = m.outputs[0]
        for given, folded in ((2.0, 2.0), (7.0, 3.0), (-7.0, -3.0), (12.0, -2.0)):
            source.outputs[0].set_voltage(given)

            def reached(expected: float = folded) -> bool:
                return out.get_voltage() == expected

            assert until(reached), (given, out.get_voltage())
        kernel = m.kernel
        assert kernel is not None
        kernel.set("gain", 2.0)  # 12 V * 2 = 24 V folds to 4 V
        assert until(lambda: out.get_voltage() == 4.0), out.get_voltage()
    finally:
        HOOKS.engine_disconnect(cable)
        HOOKS.engine_delete(m, None)
        HOOKS.engine_delete(source, None)


def test_graph_node_types_load_with_their_library() -> None:
    types = {t.name: t for t in node_types()}
    assert set(types) >= {"example.Fold", "example.Hold"}
    assert types["example.Fold"].inputs == 1 and types["example.Fold"].args == ("drive", "limit")
    assert types["example.Hold"].inputs == 2 and types["example.Hold"].args == ()
    assert not {name for name in types if name.startswith(("graphabi.", "graphbad."))}
    kernels = {t.name for t in kernel_types()}
    assert "graphabi.Kept" not in kernels and "graphbad.Kept" not in kernels  # all or nothing per library


def stepped(m: Module, voltages: list[list[float]], frames: int) -> list[list[float]]:
    out = []
    for frame in range(frames):
        for port, v in enumerate(voltages):
            m.inputs[port].channels = 1
            m.inputs[port].set_voltage(v[frame])
        HOOKS.module_process(m, 48000.0, frame)
        out.append([m.outputs[0].get_voltage(c) for c in range(m.outputs[0].get_channels())])
    return out


def test_a_graph_node_matches_the_library_kernel() -> None:
    frames = 400
    x = [f32(12.0 * math.sin(k / 9.0)) for k in range(frames)]
    g = Graph()
    g.output(0, g.node("example.Fold", g.input(0), limit=5.0, drive=2.0))
    graphed = Module.create(lambda m: m.config(0, 1, 1, 0))
    kernel = Module.create(lambda m: m.config(0, 1, 1, 0))
    try:
        graphed.outputs[0].channels = kernel.outputs[0].channels = 1
        graphed.set_kernel(g.compile())
        kernel.set_kernel(load("example.Fold")(input=0, output=0, gain=2.0, limit=5.0))
        assert stepped(graphed, [x], frames) == stepped(kernel, [x], frames)
    finally:
        graphed.close()
        kernel.close()


def test_a_stateful_graph_node_holds_its_samples() -> None:
    g = Graph()
    g.output(0, g.node("example.Hold", g.input(0), g.input(1)))
    m = Module.create(lambda m: m.config(0, 2, 1, 0))
    try:
        m.outputs[0].channels = 1
        m.set_kernel(g.compile())
        signal = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0]
        trigger = [0.0, 2.0, 2.0, 0.0, 2.0, 0.0]
        assert stepped(m, [signal, trigger], 6) == [[0.0], [2.0], [2.0], [2.0], [5.0], [5.0]]
    finally:
        m.close()


def test_graph_nodes_are_checked_when_compiled() -> None:
    cases: list[tuple[Callable[[Graph], Signal], str]] = [
        (lambda g: g.node("example.Missing", g.input(0)), "no native node type has this name"),
        (lambda g: g.node("example.Fold", g.input(0), drive=1.0), 'needs "limit"'),
        (lambda g: g.node("example.Fold", g.input(0), g.input(0), drive=1.0, limit=1.0), "reads 1 signals, not 2"),
        (lambda g: g.node("example.Fold", g.input(0), drive=1.0, limit=0.0), "limit must be positive"),
    ]
    for build, message in cases:
        g = Graph()
        g.output(0, build(g))
        with pytest.raises(ValueError, match=message):
            g.compile()


def run_frames(m: Module, inputs: list[list[list[float]]], params: list[float], rates: list[float]) -> list[object]:
    """Steps a Python-owned module; each frame's output channels and voltages, and light 0."""
    frames: list[object] = []
    for frame, rate in enumerate(rates):
        for port, values in enumerate(inputs):
            m.inputs[port].channels = len(values[frame])
            for c, v in enumerate(values[frame]):
                m.inputs[port].set_voltage(v, c)
        if len(m.params):
            m.params[0].value = params[frame]
        if frame == 150 and m.kernel is not None and "level" in m.kernel.values:
            m.kernel.set("level", -1.25)
        HOOKS.module_process(m, rate, frame)
        outs = [[o.get_voltage(c) for c in range(o.get_channels())] for o in m.outputs]
        frames.append((outs, [light.get_brightness() for light in m.lights]))
    return frames


@pytest.mark.parametrize("name", sorted(GRAPHS))
def test_compiled_kernels_match_the_plan_interpreter(name: str) -> None:
    """The same graph in both tiers: bit for bit, on the single-channel path and the polyphonic one."""
    params, ins, outs, lights = CONFIG[name]
    rng = random.Random(name)
    frames = 300
    # Frames 0-99 single channel or none; then up to 16 channels; then single again.
    widths = [rng.choice([0, 1]) if (k < 100 or k >= 200) else rng.choice([0, 1, 3, 16]) for k in range(frames)]
    inputs = [[[f32(rng.uniform(-3, 3)) for _ in range(widths[k])] for k in range(frames)] for _ in range(ins)]
    values = [f32(rng.uniform(-1, 1)) for _ in range(frames)]
    rates = [48000.0] * 220 + [96000.0] * 80
    compiled = Module.create(lambda m: m.config(params, ins, outs, lights))
    interpreted = Module.create(lambda m: m.config(params, ins, outs, lights))
    try:
        for m in (compiled, interpreted):
            for o in m.outputs:
                o.channels = 1
        kernel = load(f"graphs.{name}")()
        assert kernel.values == GRAPHS[name].compile().values  # the same wiring and controls
        compiled.set_kernel(kernel)
        interpreted.set_kernel(GRAPHS[name].compile())
        a = run_frames(compiled, inputs, values, rates)
        b = run_frames(interpreted, inputs, values, rates)
    finally:
        compiled.close()
        interpreted.close()
    assert len(a) == frames
    for frame, (x, y) in enumerate(zip(a, b, strict=True)):
        assert repr(x) == repr(y), (frame, x, y)  # repr: NaN and -0.0 compare too


def test_compiled_kernels_refuse_unwired_fields() -> None:
    with pytest.raises(ValueError, match=r"graphs\.Vca: input\[0\] must be wired"):
        load("graphs.Vca")(**{"input[0]": -1})
