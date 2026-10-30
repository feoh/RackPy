# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-17B: a plugin's own C++ kernel library, run by Rack's engine (live UI suite).

tests/dsp/test_runtime.py stages the example library and several broken ones;
only the example's types may appear. RP-21C: the example's graph node types
too, used from Python graphs, and the "graphs" library that bin/rackpy-graph
compiled from graphs.py, each kernel checked against the plan interpreter.
RP-23C: compiled expander nodes in Rack's own engine, and the "levels"
library exchanging messages under a schema of its own.
"""

import contextlib
import math
import random
import runpy
import time
from collections.abc import Callable, Iterator
from pathlib import Path

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.dsp.graph import Graph, Signal, node_types
from rackpy.dsp.graph_reference import f32, run_pair
from rackpy.dsp.kernel import kernel_types, load
from rackpy.engine.expanders import allocate_messages, graph_messages
from rackpy.raw.engine import Engine, Module

# The graphs bin/rackpy-graph compiled into the "graphs" kernel library (tests/dsp/kernel_library/graphs.cpp).
_GRAPHS = runpy.run_path(str(Path(__file__).with_name("graphs.py")))
GRAPHS: dict[str, Graph] = _GRAPHS["GRAPHS"]
CONFIG: dict[str, tuple[int, int, int, int]] = _GRAPHS["CONFIG"]
PAIRS: dict[tuple[str, str], tuple[tuple[int, int], tuple[int, int]]] = _GRAPHS["PAIRS"]


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
    refused_types = ("wrongabi.Never", "throws.Partial", "badname.Good", "rackpy.Gain2")
    for refused in (*refused_types, "expanderabi.Kept", "expanderpart.Kept", "ioabi.Kept", "iopart.Kept"):
        assert refused not in names
    fold = load("example.Fold")
    assert fold.controls == ("gain", "limit") and [f[0] for f in fold.fields] == ["input", "output", "gain", "limit"]
    with pytest.raises(ValueError, match="limit must be positive"):
        fold(input=0, output=0, limit=0)  # the library's own exception, as ValueError


def test_library_receives_io_services() -> None:
    probe = load("iotest.Probe")

    def configure(m: Module) -> None:
        m.config(0, 0, 1, 0)
        m.set_kernel(probe(output=0))

    module = Module.create(configure)
    HOOKS.engine_adopt(module)
    try:
        assert until(lambda: module.outputs[0].get_voltage() == 10.0)
    finally:
        HOOKS.engine_delete(module, None)


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


def test_io_graphs_deliver_timed_midi_in_both_tiers() -> None:
    modules = [Module.create(lambda m: m.config(0, 1, 3, 0)) for _ in range(2)]
    queues = [raw.midi.InputQueue() for _ in modules]
    try:
        for m, queue, kernel in zip(
            modules, queues, (GRAPHS["IOGraph"].compile(), load("graphs.IOGraph")()), strict=True
        ):
            m.set_midi_input(0, queue)
            m.set_kernel(kernel)
            for output in m.outputs:
                output.channels = 1
            for status, frame in ((0x90, 3), (0x80, 7)):
                message = raw.midi.Message()
                message.bytes = [status, 64, 100 if status == 0x90 else 0]
                message.frame = frame
                queue.on_message(message)
        results: list[list[list[float]]] = [[], []]
        for frame in range(10):
            for i, m in enumerate(modules):
                HOOKS.module_process(m, 48000, frame)
                results[i].append([m.outputs[k].get_voltage() for k in range(3)])
        assert results[0] == results[1]
        assert [row[1] for row in results[0]] == [0.0] * 3 + [10.0] * 4 + [0.0] * 3
        assert results[0][3][0] == pytest.approx(4 / 12)
        assert all(row[2] == 0.0 for row in results[0])  # no audio port
    finally:
        for m in modules:
            m.set_midi_input(0, None)
            m.close()


def test_native_io_plan_rejects_invalid_settings() -> None:
    from _rackpy_host import dsp

    valid = GRAPHS["IOGraph"].plan().to_dict()
    midi_node = next(n for n in valid["nodes"] if n["op"] == "midi_cv")
    midi_node["args"][0] = 4
    with pytest.raises(ValueError, match="setting 0"):
        dsp.graph_kernel(valid)
    valid = GRAPHS["IOGraph"].plan().to_dict()
    audio_node = next(n for n in valid["nodes"] if n["op"] == "audio_output")
    audio_node["args"][0] = -1
    with pytest.raises(ValueError, match="channel"):
        dsp.graph_kernel(valid)


def test_compiled_kernels_refuse_unwired_fields() -> None:
    with pytest.raises(ValueError, match=r"graphs\.Vca: input\[0\] must be wired"):
        load("graphs.Vca")(**{"input[0]": -1})


# --- RP-23C: expanders in kernel libraries ----------------------------------------------------


@contextlib.contextmanager
def stepped_by_the_test() -> Iterator[Engine]:
    """Rack's engine, stepped only by Engine.step_block from here (a RackPy master module stops its thread)."""
    context = raw.context_get()
    assert context is not None and context.engine is not None
    e = context.engine
    previous = e.get_master_module()
    clock = Module.create(lambda m: m.config(0, 0, 0, 0))
    HOOKS.engine_adopt(clock)
    e.set_master_module(clock)
    try:
        frame = e.get_frame()
        for _ in range(100):  # until Rack's own engine thread is idle
            time.sleep(0.01)
            if e.get_frame() == frame:
                break
            frame = e.get_frame()
        else:
            pytest.fail("Rack's engine kept stepping after a RackPy module became its master")
        yield e
    finally:
        e.set_master_module(previous)
        HOOKS.engine_delete(clock)


def adjacent(left: Module, right: Module) -> None:
    HOOKS.rack_set_expander_id(left, 1, right.id)
    HOOKS.rack_set_expander_id(right, 0, left.id)


def outputs(m: Module) -> list[list[float]]:
    return [[o.get_voltage(c) for c in range(max(1, o.get_channels()))] for o in m.outputs]


def pair(compiled: bool) -> tuple[Module, Module]:
    (left, right), ((left_side, left_lanes), (right_side, right_lanes)) = next(iter(PAIRS.items()))
    made = []
    for name, side, lanes in ((left, left_side, left_lanes), (right, right_side, right_lanes)):
        params, ins, outs, lights = CONFIG[name]

        def configure(m: Module, p: int = params, i: int = ins, o: int = outs, li: int = lights) -> None:
            m.config(p, i, o, li)

        m = Module.create(configure)
        graph_messages(m.get_expander(side), lanes)
        m.get_expander(side).snapshots = True
        m.set_kernel(load(f"graphs.{name}")() if compiled else GRAPHS[name].compile())
        made.append(m)
    return made[0], made[1]


def test_compiled_expander_graphs_match_the_interpreter_in_racks_engine() -> None:
    """graphs.Sender | graphs.Echo, compiled and interpreted, side by side in Rack's engine, frame for frame."""
    frames = 64
    (left, right), _ = next(iter(PAIRS.items()))
    a, b = run_pair(GRAPHS[left], GRAPHS[right], frames)
    reference = [([a.outputs[0][n]], [b.outputs[0][n], b.outputs[1][n]]) for n in range(frames)]
    with stepped_by_the_test() as e:
        compiled, interpreted = pair(True), pair(False)
        modules = [*compiled, *interpreted]
        for m in modules:
            HOOKS.engine_adopt(m)
        try:
            adjacent(*compiled)
            adjacent(*interpreted)
            seen: list[tuple[int, list[float]] | None] = []
            for n in range(frames):
                e.step_block(1)
                got = (outputs(compiled[0]), outputs(compiled[1]))
                assert got == (outputs(interpreted[0]), outputs(interpreted[1])), n
                assert got == reference[n], n
                # Snapshots of what each receiver got this frame, the same in both tiers.
                snapshots = [compiled[0].right_expander.snapshot(), compiled[1].left_expander.snapshot()]
                assert snapshots == [interpreted[0].right_expander.snapshot(), interpreted[1].left_expander.snapshot()]
                seen.append(snapshots[1])
            counters = [s[1][18] for s in seen if s is not None]  # lane 1, channel 0 of the right module's message
            assert counters[-5:] == [float(n) for n in range(frames - 5, frames)]
        finally:
            for m in modules:
                HOOKS.engine_delete(m)


def test_a_library_exchanges_under_its_own_schema() -> None:
    """levels.Send | levels.Receive in Rack's engine: one frame for the cable, one for the expander."""
    send, receive = load("levels.Send"), load("levels.Receive")
    source = Module.create(lambda m: m.config(0, 0, 1, 0))
    sender = Module.create(lambda m: m.config(0, 1, 0, 0))
    sender.set_kernel(send(input=0))
    receiver = Module.create(lambda m: m.config(0, 0, 1, 0))
    allocate_messages(receiver.left_expander, "levels.Levels", 17)
    receiver.set_kernel(receive(output=0))
    other = Module.create(lambda m: m.config(0, 0, 1, 0))
    graph_messages(other.left_expander)  # "rackpy.graph", not "levels.Levels": nothing is written into it
    other.set_kernel(receive(output=0))
    for m in (receiver, other):
        m.outputs[0].channels = 1  # patched, so that Rack's setChannels keeps the received count

    def drive(n: int) -> list[float]:
        return [float(n), n + 0.5, -float(n)]

    with stepped_by_the_test() as e:
        modules = [source, sender, receiver, other]
        for m in modules:
            HOOKS.engine_adopt(m)
        cable = HOOKS.engine_connect(source, 0, sender, 0)
        try:
            adjacent(sender, receiver)
            for n in range(12):
                out = source.outputs[0]
                out.set_channels(3)
                for c, v in enumerate(drive(n)):
                    out.set_voltage(v, c)
                e.step_block(1)
                expected = drive(n - 2) if n >= 2 else [0.0]
                assert outputs(receiver) == [expected], n
            adjacent(sender, other)  # the sender now faces a module with the wrong schema
            HOOKS.rack_set_expander_id(receiver, 0, -1)
            e.step_block(2)
            assert outputs(other) == [[0.0]] and outputs(receiver) == [[0.0]]
        finally:
            HOOKS.engine_disconnect(cable)
            for m in modules:
                HOOKS.engine_delete(m)
    with pytest.raises(ValueError, match=r"levels\.Send: input must be wired"):
        send(input=-1)  # the library's own check, as ValueError
