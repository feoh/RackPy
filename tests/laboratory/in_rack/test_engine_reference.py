# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22A: a Bench steps native modules exactly as Rack's own engine does (live UI suite only).

The same patch of native RackPy modules (kernels, a bypass route, a
Python-written source) runs in Rack's engine, stepped one frame at a time by
Engine.step_block with a RackPy master module, and on a laboratory Bench.
Every port of every module must agree after every frame. The test hooks put
the modules in Rack's engine; the production start-up run ignores this file.
"""

import math
import time
from collections.abc import Callable
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.dsp import kernel
from rackpy.laboratory import Bench
from rackpy.raw.engine import Engine, Module

FRAMES = 96
BYPASSED = range(30, 50)  # gain_b is bypassed for these frames


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def patch() -> dict[str, Module]:
    """Fresh, Python-owned modules: a source Python writes, a sine and two gains (one with a bypass route)."""

    def source(m: Module) -> None:
        m.config(0, 0, 2, 0)

    def sine(m: Module) -> None:
        m.config(1, 1, 1, 0)
        m.config_param(0, -2, 2, 0.25)
        m.set_kernel(kernel.Sine(pitch_input=0, output=0, pitch_param=0, amplitude=5))

    def gain(factor: float, bypass: bool) -> Callable[[Module], None]:
        def configure(m: Module) -> None:
            m.config(0, 1, 1, 0)
            if bypass:
                m.config_bypass(0, 0)
            m.set_kernel(kernel.Gain(input=0, output=0, gain=factor))

        return configure

    return {
        "source": Module.create(source),
        "sine": Module.create(sine),
        "gain_a": Module.create(gain(0.5, False)),
        "gain_b": Module.create(gain(-1.25, True)),
    }


# (output module, output, input module, input): stacked inputs, polyphony and a feedback loop.
CABLES = [
    ("source", 0, "gain_a", 0),
    ("gain_a", 0, "sine", 0),
    ("sine", 0, "gain_b", 0),
    ("source", 1, "gain_b", 0),
    ("gain_b", 0, "gain_a", 0),
]


def drive(modules: dict[str, Module], frame: int) -> None:
    """What the source module outputs at `frame`: four channels (with non-finite values) and one."""
    poly, mono = modules["source"].outputs[0], modules["source"].outputs[1]
    poly.set_channels(4)
    for c in range(4):
        poly.set_voltage(math.sin(frame * 0.21 + c) * (c + 1), c)
    if frame % 17 == 5:
        poly.set_voltage(math.inf, 1)
    if frame % 23 == 7:
        poly.set_voltage(math.nan, 3)
    mono.set_voltage(0.125 * (frame % 8))


def snapshot(modules: dict[str, Module]) -> list[Any]:
    ports = []
    for m in modules.values():
        for port in [*m.inputs, *m.outputs]:
            channels = port.get_channels()
            ports.append((channels, [port.get_voltage(c) for c in range(max(channels, 1))]))
    return ports


def run_in_racks_engine() -> list[Any]:
    e = engine()
    previous = e.get_master_module()
    clock = Module.create(lambda m: m.config(0, 0, 0, 0))
    HOOKS.engine_adopt(clock)
    e.set_master_module(clock)  # Rack's own engine thread stops stepping
    modules: dict[str, Module] = {}
    cables: list[int] = []
    try:
        frame = e.get_frame()
        for _ in range(100):  # until Rack's thread is idle
            time.sleep(0.01)
            if e.get_frame() == frame:
                break
            frame = e.get_frame()
        else:
            pytest.fail("Rack's engine kept stepping after a RackPy module became its master")
        modules = patch()
        for m in modules.values():
            HOOKS.engine_adopt(m)
        cables = [HOOKS.engine_connect(modules[o], oi, modules[i], ii) for o, oi, i, ii in CABLES]
        frames = []
        for f in range(FRAMES):
            if f == BYPASSED.start:
                HOOKS.engine_bypass(modules["gain_b"], True)
            if f == BYPASSED.stop:
                HOOKS.engine_bypass(modules["gain_b"], False)
            drive(modules, f)
            e.step_block(1)
            frames.append(snapshot(modules))
        return frames
    finally:
        for cable in cables:
            HOOKS.engine_disconnect(cable)
        e.set_master_module(previous)
        for m in [*modules.values(), clock]:
            HOOKS.engine_delete(m)


def run_on_a_bench() -> list[Any]:
    bench = Bench(engine().get_sample_rate())
    modules = patch()
    for m in modules.values():
        bench.add(m)
    for o, oi, i, ii in CABLES:
        bench.connect(modules[o], oi, modules[i], ii)
    frames = []
    for f in range(FRAMES):
        if f == BYPASSED.start:
            bench.set_bypassed(modules["gain_b"], True)
        if f == BYPASSED.stop:
            bench.set_bypassed(modules["gain_b"], False)
        drive(modules, f)
        bench.step()
        frames.append(snapshot(modules))
    bench.clear()
    return frames


def test_a_bench_reproduces_racks_engine_frame_for_frame() -> None:
    reference = run_in_racks_engine()
    stepped = run_on_a_bench()
    assert len(reference) == len(stepped) == FRAMES
    for f, (expected, actual) in enumerate(zip(reference, stepped, strict=True)):
        assert repr(actual) == repr(expected), f"frame {f}"  # repr: NaN compares equal too
    # The patch exercised what it claims: polyphony and non-finite source voltages.
    flat = repr(reference)
    assert "inf" in flat and "nan" in flat
    assert any(port[0] == 4 for frame in reference for port in frame)


def test_a_laboratory_module_never_goes_to_racks_engine() -> None:
    from rackpy.errors import LaboratoryError
    from rackpy.laboratory import ModuleClass

    class Lab(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

    lab = Lab.create()
    with pytest.raises(LaboratoryError, match="never in Rack's engine"):
        HOOKS.engine_adopt(lab.module)
    with pytest.raises(LaboratoryError, match="never in Rack's engine"):
        engine().add_module(lab.module)
