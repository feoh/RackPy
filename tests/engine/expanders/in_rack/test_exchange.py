# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-23B/C: DSP graph kernels exchange expander messages in Rack's own engine (live UI, test hooks).

Two RackPy modules side by side in Rack's engine run graph kernels that send
to and receive from each other through ``"rackpy.graph"`` messages. Rack's
engine is stepped by the test (a RackPy master module stops Rack's own
thread, then ``Engine.step_block``), at several sample rates and engine
thread counts, one frame at a time and in blocks. Every frame must equal the
offline reference's pair (``graph_reference.run_pair``): exactly one frame
between a send and its arrival. The test hooks place modules, as Rack's
``RackWidget::updateExpanders`` does for adjacent module widgets.
"""

import contextlib
import time
from collections.abc import Iterator

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import laboratory, raw
from rackpy.dsp import graph_reference
from rackpy.dsp.graph import Graph, Signal
from rackpy.engine.expanders import allocate_messages, graph_messages
from rackpy.errors import OwnershipError
from rackpy.laboratory import Bench
from rackpy.raw import settings as rset
from rackpy.raw.engine import Engine, Module

FRAMES = 48
BLOCK = 256


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


@contextlib.contextmanager
def stepped_by_the_test() -> Iterator[Engine]:
    """Rack's engine, stepped only by Engine.step_block from here; its rate and thread count restored afterwards."""
    e = engine()
    previous, rate, threads = e.get_master_module(), HOOKS.engine_sample_rate(), rset.settings.thread_count
    clock = Module.create(lambda m: m.config(0, 0, 0, 0))
    HOOKS.engine_adopt(clock)
    e.set_master_module(clock)  # Rack's own engine thread stops stepping
    try:
        frame = e.get_frame()
        for _ in range(100):
            time.sleep(0.01)
            if e.get_frame() == frame:
                break
            frame = e.get_frame()
        else:
            pytest.fail("Rack's engine kept stepping after a RackPy module became its master")
        yield e
    finally:
        rset.settings.thread_count = threads
        e.set_master_module(previous)
        HOOKS.engine_set_sample_rate(rate)
        HOOKS.engine_delete(clock)


def counter(g: Graph, start: float) -> Signal:
    count = g.delay(start)
    count.feed(count + 1)
    return count


def left_graph(start: float = 1.0) -> Graph:
    """Sends a counter right; outputs what comes back from the right."""
    g = Graph()
    g.expander_send(1, counter(g, start))
    g.output(0, g.expander_receive(1))
    return g


def right_graph() -> Graph:
    """Outputs what arrives from the left and its own counter; echoes what arrived plus 100."""
    g = Graph()
    got = g.expander_receive(0)
    g.output(0, got)
    g.output(1, counter(g, 1.0))
    g.expander_send(0, got + 100)
    return g


def module(graph: Graph, outputs: int, sides: tuple[int, ...]) -> Module:
    m = Module.create(lambda m: m.config(0, 0, outputs, 0))
    for side in sides:
        graph_messages(m.get_expander(side))
    m.set_kernel(graph.compile())
    return m


def adjacent(left: Module, right: Module) -> None:
    HOOKS.rack_set_expander_id(left, 1, right.id)
    HOOKS.rack_set_expander_id(right, 0, left.id)


def outputs(m: Module) -> list[list[float]]:
    return [[o.get_voltage(c) for c in range(max(1, o.get_channels()))] for o in m.outputs]


def reference(frames: int) -> list[tuple[list[list[float]], list[list[float]]]]:
    a, b = graph_reference.run_pair(left_graph(), right_graph(), frames)
    return [([a.outputs[0][n]], [b.outputs[0][n], b.outputs[1][n]]) for n in range(frames)]


@pytest.mark.parametrize("threads", [1, 2, 4])
@pytest.mark.parametrize("rate", [44100.0, 48000.0, 96000.0])
def test_a_pair_in_racks_engine_is_one_frame_apart(rate: float, threads: int) -> None:
    expected = reference(FRAMES)
    with stepped_by_the_test() as e:
        HOOKS.engine_set_sample_rate(rate)
        rset.settings.thread_count = threads  # Rack's engine relaunches its workers at the next block
        a, b = module(left_graph(), 1, (1,)), module(right_graph(), 2, (0,))
        HOOKS.engine_adopt(a)
        HOOKS.engine_adopt(b)
        try:
            adjacent(a, b)
            frames = []
            for _ in range(FRAMES):
                e.step_block(1)
                frames.append((outputs(a), outputs(b)))
            assert e.get_sample_rate() == rate
            assert frames == expected
            # In blocks: the latency stays one frame whatever the block size.
            e.step_block(BLOCK)
            n = FRAMES + BLOCK - 1  # the last frame processed
            assert outputs(b) == [[float(n)], [float(n + 1)]]
            assert outputs(a) == [[float(n - 1) + 100]]
        finally:
            HOOKS.engine_delete(a)
            HOOKS.engine_delete(b)


def test_a_laboratory_bench_matches_racks_engine() -> None:
    laboratory.enable()
    try:
        a, b = module(left_graph(), 1, (1,)), module(right_graph(), 2, (0,))
        bench = Bench(48000)
        bench.add(a)
        bench.add(b)
        bench.set_expander(a, 1, b)
        bench.set_expander(b, 0, a)
        frames = []
        for _ in range(FRAMES):
            bench.step()
            frames.append((outputs(a), outputs(b)))
        bench.clear()
    finally:
        laboratory.disable()
    assert frames == reference(FRAMES)


def test_removal_reordering_and_a_schema_mismatch_give_silence_or_the_new_neighbour() -> None:
    with stepped_by_the_test() as e:
        a, b, c = module(left_graph(), 1, (1,)), module(right_graph(), 2, (0,)), module(left_graph(1000.0), 1, (1,))
        for m in (a, b, c):
            HOOKS.engine_adopt(m)
        removed = False
        try:
            adjacent(a, b)
            e.step_block(10)
            assert outputs(b)[0] == [9.0]  # a's counter of frame 8
            # Reordered: c moves in on b's left; a now has no right neighbour. c has run since frame 0,
            # sending at nothing until now.
            HOOKS.rack_set_expander_id(a, 1, -1)
            adjacent(c, b)
            e.step_block(1)
            assert outputs(b)[0] == [10.0]  # frame 10: what a sent during frame 9, already flipped
            e.step_block(1)
            assert outputs(b)[0] == [1010.0]  # frame 11: c's counter of frame 10
            # Removed: Rack clears b's left expander; b hears nothing, at once.
            HOOKS.engine_delete(c)
            removed = True
            e.step_block(1)
            assert outputs(b)[0] == [0.0]
            assert b.left_expander.module is None and b.left_expander.module_id == -1
        finally:
            for m in (a, b) if removed else (a, b, c):
                HOOKS.engine_delete(m)
    # A neighbour whose messages have another schema: nothing is written into them, nothing is received.
    with stepped_by_the_test() as e:
        a = module(left_graph(), 1, (1,))
        b = Module.create(lambda m: m.config(0, 0, 2, 0))
        allocate_messages(b.left_expander, "demo.Other", 17)
        b.set_kernel(right_graph().compile())
        HOOKS.engine_adopt(a)
        HOOKS.engine_adopt(b)
        try:
            adjacent(a, b)
            e.step_block(16)
            assert outputs(b)[0] == [0.0] and outputs(b)[1] == [16.0]
            assert outputs(a) == [[100.0]]  # b still answers a: the silence it got, plus 100
        finally:
            HOOKS.engine_delete(a)
            HOOKS.engine_delete(b)


def test_python_reads_snapshots_while_racks_engine_runs() -> None:
    """RP-23C: Rack's own engine thread runs the pair; Python reads consistent snapshots meanwhile."""
    a, b = module(left_graph(), 1, (1,)), module(right_graph(), 2, (0,))
    assert b.left_expander.snapshot() is None and not b.left_expander.snapshots
    b.left_expander.snapshots = True
    HOOKS.engine_adopt(a)
    HOOKS.engine_adopt(b)
    try:
        adjacent(a, b)  # Rack's engine thread follows the IDs at its next block
        e = engine()
        start = e.get_frame()
        offsets: set[float] = set()
        frames: list[int] = []
        deadline = time.monotonic() + 10
        while len(frames) < 200 and time.monotonic() < deadline:
            snapshot = b.left_expander.snapshot()  # while the engine thread may be writing it
            if snapshot is not None:
                frame, values = snapshot
                assert len(values) == 17 and values[0] == 1.0  # one channel: a's counter
                offsets.add(values[1] - frame)  # its counter of the frame before: a constant offset
                frames.append(frame)
            time.sleep(0.001)
        assert len(frames) == 200, "Rack's engine did not run the pair"
        assert frames == sorted(frames) and frames[-1] > frames[0] >= start
        assert len(offsets) == 1, offsets  # never a torn or mixed snapshot
        b.left_expander.snapshots = False
        held = b.left_expander.snapshot()
        time.sleep(0.05)
        assert b.left_expander.snapshot() == held  # off: no longer written
        with pytest.raises(OwnershipError, match="engine runs this module"):
            b.left_expander.consumer_message  # noqa: B018 - the property raises
    finally:
        HOOKS.engine_delete(a)
        HOOKS.engine_delete(b)


def test_snapshots_are_a_rackpy_modules() -> None:
    handle = HOOKS.add_probe_module()
    try:
        context = raw.context_get()
        assert context is not None and context.engine is not None
        probe = context.engine.get_module(handle.module_id)
        assert probe is not None
        with pytest.raises(OwnershipError, match="only a RackPy module's expander has snapshots"):
            probe.left_expander.snapshot()
        with pytest.raises(OwnershipError, match="only a RackPy module's expander has snapshots"):
            probe.left_expander.snapshots = True
    finally:
        HOOKS.remove_probe_module(handle)
    m = Module.create(lambda m: m.config(0, 0, 0, 0))
    try:
        m.right_expander.snapshots = True  # no messages yet: nothing to snapshot
        assert m.right_expander.snapshots and m.right_expander.snapshot() is None
    finally:
        m.close()
