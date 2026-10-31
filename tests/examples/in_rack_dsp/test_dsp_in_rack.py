# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-36: the polyphony, DSP and expanders examples' modules inside Rack's live UI.

The plugin's entry setup imported the three examples, after RackPy loaded their kernel
libraries. Modules are made from the examples' models. The oscillators are stepped on
the main thread and compared with the C++ reference; the filter with the same Rack
filter code run from Python; the expanders in Rack's own engine, stepped by the test,
on 1, 2 and 4 engine threads.
"""

import contextlib
import math
import time
import warnings
from collections.abc import Iterator
from typing import Any

import pytest
import rackpydsp_teaching as teaching
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.dsp.graph import Graph
from rackpy.raw import settings as rset
from rackpy.raw.engine import Engine, Module, Port


def make(slug: str) -> Module:
    model = raw.plugin.get_model("RackPy", slug)
    assert model is not None, slug
    module = model.create_module()
    assert isinstance(module, Module)
    return module


def patch(port: Port, channels: int) -> None:
    """Marks a port as Rack does when a cable connects: set_channels cannot, as an unpatched port keeps 0 channels."""
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)  # writing Port.channels directly is deprecated in Rack
        port.channels = channels


def voltages(port: Port) -> list[float]:
    return [port.get_voltage(c) for c in range(port.get_channels())]


@pytest.mark.parametrize(("slug", "variant"), [("PolySine", "simd"), ("PolySineScalar", "scalar")])
def test_the_oscillators_match_the_cpp_reference(polyphony_reference: dict[str, Any], slug: str, variant: str) -> None:
    rate = polyphony_reference["rate"]
    for scenario in polyphony_reference["scenarios"]:
        m = make(slug)
        try:
            m.params[0].value = scenario["knob"]
            patch(m.outputs[0], 1)
            worst = 0.0
            for k, (volts, want) in enumerate(zip(scenario["inputs"], scenario[variant], strict=True)):
                patch(m.inputs[0], len(volts))
                for c, v in enumerate(volts):
                    m.inputs[0].set_voltage(v, c)
                HOOKS.module_process(m, rate, k)
                got = voltages(m.outputs[0])
                assert len(got) == len(want), (scenario["name"], k)
                worst = max([worst, *(abs(a - b) for a, b in zip(got, want, strict=True))])
            print(f"{slug} {scenario['name']}: largest difference from C++ {worst:.3g} V")
            assert worst <= 1e-5, scenario["name"]
        finally:
            m.close()


FILTER_SCENARIOS = [
    # name, channels, mode, cutoff, resonance, (frame, new cutoff)
    ("unpatched lowpass", 0, 0, 5.0, 0.707, None),
    ("mono lowpass, cutoff moved", 1, 0, 4.0, 2.0, (500, 7.0)),
    ("five voices highpass", 5, 1, 6.0, 0.707, None),
    ("sixteen voices bandpass", 16, 2, 5.5, 4.0, None),
    ("sixteen voices notch", 16, 3, 6.5, 1.0, (777, 3.0)),
]


@pytest.mark.parametrize("scenario", FILTER_SCENARIOS, ids=[s[0] for s in FILTER_SCENARIOS])
def test_the_filter_kernel_matches_racks_filter_run_from_python(scenario: tuple[Any, ...]) -> None:
    name, channels, mode, cutoff, resonance, change = scenario
    rate, frames = 48000.0, 1200
    tone = teaching.tone(frames, rate, 1500.0, channels)
    knob = [cutoff if change is None or k < change[0] else change[1] for k in range(frames)]
    expected = teaching.run(knob, resonance, mode, tone, rate)
    m = make("Filter")
    try:
        m.params[0].value, m.params[1].value, m.params[2].value = cutoff, resonance, mode
        patch(m.outputs[0], 1)
        patch(m.inputs[0], channels)
        worst = 0.0
        for k, volts in enumerate(tone):
            if change is not None and k == change[0]:
                m.params[0].value = change[1]
            for c, v in enumerate(volts):
                m.inputs[0].set_voltage(v, c)
            HOOKS.module_process(m, rate, k)
            got, want = voltages(m.outputs[0]), expected[k]
            assert len(got) == len(want), k
            worst = max([worst, *(abs(a - b) for a, b in zip(got, want, strict=True))])
        print(f"Filter {name}: largest difference from Rack's filter run from Python {worst:.3g} V")
        # The same Rack filter code, compiled separately with the SDK's -funsafe-math-optimizations;
        # a resonant filter amplifies the rounding. A timing or wiring mistake would be volts off.
        assert worst <= 1e-3, name
    finally:
        m.close()


# --- expanders, in Rack's own engine -------------------------------------------------------


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


@contextlib.contextmanager
def stepped_by_the_test(threads: int) -> Iterator[Engine]:
    """Rack's engine, stepped only by Engine.step_block from here; its thread count restored afterwards."""
    e = engine()
    previous, old_threads = e.get_master_module(), rset.settings.thread_count
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
        rset.settings.thread_count = threads  # Rack relaunches its workers at the next block
        yield e
    finally:
        rset.settings.thread_count = old_threads
        e.set_master_module(previous)
        HOOKS.engine_delete(clock)


def feeder(start: float) -> Module:
    """A counter, start, start + 1, ..., on its output: something to cable into a Source."""
    g = Graph()
    count = g.delay(start)
    count.feed(count + 1)
    g.output(0, count)
    m = Module.create(lambda m: m.config(0, 0, 1, 0))
    m.set_kernel(g.compile())
    return m


def beside(left: Module, right: Module) -> None:
    """As Rack's rack does for adjacent module widgets."""
    HOOKS.rack_set_expander_id(left, 1, right.id)
    HOOKS.rack_set_expander_id(right, 0, left.id)


def apart(left: Module, right: Module) -> None:
    HOOKS.rack_set_expander_id(left, 1, -1)
    HOOKS.rack_set_expander_id(right, 0, -1)


@pytest.mark.parametrize("threads", [1, 2, 4])
def test_the_source_reaches_the_receiver_one_frame_later(threads: int) -> None:
    with stepped_by_the_test(threads) as e:
        feed, other_feed = feeder(1.0), feeder(1000.0)
        source, other, receiver = make("Source"), make("Source"), make("Receiver")
        source.params[0].value, other.params[0].value = 0.5, 0.25
        modules = [feed, other_feed, source, other, receiver]
        for m in modules:
            HOOKS.engine_adopt(m)
        cables = [HOOKS.engine_connect(feed, 0, source, 0), HOOKS.engine_connect(other_feed, 0, other, 0)]
        try:
            beside(source, receiver)
            sent: list[float] = []
            got: list[float] = []
            for _ in range(40):
                e.step_block(1)
                sent.append(source.outputs[0].get_voltage())
                got.append(receiver.outputs[0].get_voltage())
            assert got[1:] == sent[:-1] and len(set(sent)) > 30  # exactly one frame later, every frame
            assert receiver.lights[0].get_brightness() == pytest.approx(0.5)  # the Source's LEVEL, sent in lane 1
            # Rearranged: the other Source moves in on the Receiver's left.
            apart(source, receiver)
            beside(other, receiver)
            e.step_block(1)  # what `source` sent during the last frame was already flipped in
            e.step_block(1)
            assert receiver.outputs[0].get_voltage() == pytest.approx(other.outputs[0].get_voltage() - 0.25)
            assert receiver.lights[0].get_brightness() == pytest.approx(0.25)
            # Removed: nothing arrives, at once.
            HOOKS.engine_disconnect(cables.pop())
            HOOKS.engine_delete(other)
            modules.remove(other)
            e.step_block(1)
            # 0 V; Rack reports an output without a cable as having no channels.
            assert receiver.outputs[0].get_voltage() == 0.0 and receiver.lights[0].get_brightness() == 0.0
        finally:
            for cable in cables:  # Rack asserts that a module it removes has no cables
                HOOKS.engine_disconnect(cable)
            for m in modules:
                HOOKS.engine_delete(m)


@pytest.mark.parametrize("threads", [1, 2, 4])
def test_the_peeker_reads_only_a_source(threads: int) -> None:
    with stepped_by_the_test(threads) as e:
        feed, source, peeker, receiver = feeder(1.0), make("Source"), make("Peeker"), make("Receiver")
        source.params[0].value = 0.3
        modules = [feed, source, peeker, receiver]
        for m in modules:
            HOOKS.engine_adopt(m)
        cables = [HOOKS.engine_connect(feed, 0, source, 0)]
        try:
            beside(source, peeker)
            for _ in range(20):
                e.step_block(1)
                seen, now = peeker.outputs[1].get_voltage(), source.outputs[0].get_voltage()
                # The Source's output of this frame or the last, as Rack processed them.
                assert seen == pytest.approx(now, abs=1e-5) or seen == pytest.approx(now - 0.3, abs=1e-5), (seen, now)
            assert peeker.outputs[0].get_voltage() == pytest.approx(3.0) and peeker.lights[0].get_brightness() == 1.0
            source.params[0].value = 0.8
            e.step_block(2)
            assert peeker.outputs[0].get_voltage() == pytest.approx(8.0)
            # Beside a Receiver: not a Source, so nothing is read.
            apart(source, peeker)
            beside(receiver, peeker)
            e.step_block(2)
            assert peeker.outputs[0].get_voltage() == 0.0 and peeker.lights[0].get_brightness() == 0.0
            # Beside the Source again, then the Source removed.
            apart(receiver, peeker)
            beside(source, peeker)
            e.step_block(2)
            assert peeker.lights[0].get_brightness() == 1.0
            HOOKS.engine_disconnect(cables.pop())
            HOOKS.engine_delete(source)
            modules.remove(source)
            e.step_block(1)
            assert peeker.outputs[0].get_voltage() == 0.0 and peeker.lights[0].get_brightness() == 0.0
            assert peeker.left_expander.module is None
        finally:
            for cable in cables:  # Rack asserts that a module it removes has no cables
                HOOKS.engine_disconnect(cable)
            for m in modules:
                HOOKS.engine_delete(m)


def test_the_filter_filters() -> None:
    """A lowpass passes a tone well below its cutoff and stops one well above it."""
    rate = 48000.0

    def level(cutoff: float, hz: float) -> float:
        m = make("Filter")
        try:
            m.params[0].value = cutoff
            patch(m.outputs[0], 1)
            patch(m.inputs[0], 1)
            peak = 0.0
            for k in range(4800):
                m.inputs[0].set_voltage(5.0 * math.sin(2 * math.pi * hz * k / rate))
                HOOKS.module_process(m, rate, k)
                if k > 2400:
                    peak = max(peak, abs(m.outputs[0].get_voltage()))
            return peak
        finally:
            m.close()

    assert level(8.0, 200.0) == pytest.approx(5.0, abs=0.1)  # 5.1 kHz cutoff: 200 Hz passes
    assert level(3.0, 5000.0) < 0.05  # 160 Hz cutoff: 5 kHz is 30 dB and more down
