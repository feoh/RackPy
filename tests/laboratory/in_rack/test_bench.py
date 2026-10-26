# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22A: the laboratory Bench and Python process/processBypass overrides, inside Rack."""

import gc
import math
import struct
import warnings
from typing import Any

import _rackpy_host
import pytest

from rackpy import laboratory, plugin
from rackpy.dsp import kernel
from rackpy.errors import LaboratoryError, OwnershipError
from rackpy.laboratory import Bench, ModuleClass, ProcessArgs
from rackpy.raw.engine import Module

CALLS: list[tuple[str, int]] = []


class Doubler(ModuleClass):
    """Output 0 is twice input 0, per channel; records each call."""

    def configure(self) -> None:
        self.module.config(0, 1, 1, 0)

    def process(self, args: ProcessArgs) -> None:
        CALLS.append((f"doubler{self.module.id}", args.frame))
        inp, out = self.module.inputs[0], self.module.outputs[0]
        channels = inp.get_channels()
        out.set_channels(channels)
        for c in range(channels):
            out.set_voltage(inp.get_voltage(c) * 2, c)


class Counter(ModuleClass):
    """Output 0 is the frame number; records the arguments Rack's engine would pass."""

    seen: list[tuple[float, float, int]]

    def configure(self) -> None:
        self.module.config(0, 0, 1, 0)
        self.seen = []

    def process(self, args: ProcessArgs) -> None:
        self.seen.append((args.sample_rate, args.sample_time, args.frame))
        CALLS.append(("counter", args.frame))
        self.module.outputs[0].set_voltage(float(args.frame))


def float32(x: float) -> float:
    """x rounded to a C++ float, as Rack stores it."""
    value: float = struct.unpack("f", struct.pack("f", x))[0]
    return value


def voltages(m: Module, output: int = 0, channels: int | None = None) -> list[float]:
    """An output's voltages: all its channels, or `channels` of them (unconnected outputs have 0, as in Rack)."""
    port = m.outputs[output]
    return [port.get_voltage(c) for c in range(port.get_channels() if channels is None else channels)]


def test_the_laboratory_starts_disabled_and_refuses_until_enabled(enabled_at_start: bool) -> None:
    assert enabled_at_start is False
    laboratory.disable()
    assert not laboratory.enabled()
    with pytest.raises(LaboratoryError, match=r"rackpy\.laboratory\.enable\(\)"):
        Doubler.create()
    with pytest.raises(LaboratoryError, match="disabled"):
        Bench()
    laboratory.enable()
    bench = Bench()
    d = Doubler.create()
    bench.add(d.module)
    laboratory.disable()
    with pytest.raises(LaboratoryError):
        bench.step()
    with pytest.raises(LaboratoryError):
        bench.add(Module.create(lambda m: m.config(0, 0, 0, 0)))
    laboratory.enable()
    bench.step()
    assert bench.frame == 1


def test_normal_mode_refuses_python_audio_callbacks() -> None:
    for name in ("process", "process_bypass", "step", "on_sample_rate_change", "on_set_master", "on_unset_master"):
        with pytest.raises(TypeError, match=f"defines {name}: Rack calls these on its audio threads") as caught:
            type("Audio", (plugin.ModuleClass,), {name: lambda self, *args: None})
        assert "rackpy.laboratory.ModuleClass" in str(caught.value)
        assert "Module.set_kernel" in str(caught.value)
    with pytest.raises(TypeError, match=r"rackpy\.laboratory runs such overrides offline"):
        Module.create(lambda m: m.set_event_handlers(on_sample_rate_change=print))  # type: ignore[call-arg]
    # A laboratory class is never a Rack model, so no patch can create one.
    with pytest.raises(LaboratoryError, match="Rack cannot create it from a model or a patch"):
        Doubler.factory()


def test_bench_steps_each_module_in_order_with_racks_arguments() -> None:
    CALLS.clear()
    bench = Bench(48000)
    counter, doubler = Counter.create(), Doubler.create()
    bench.add(counter.module)
    bench.add(doubler.module)
    assert bench.modules == (counter.module, doubler.module)
    assert (counter.module.id, doubler.module.id) == (0, 1)
    cable = bench.connect(counter.module, 0, doubler.module, 0)
    assert bench.cables == [(cable, counter.module, 0, doubler.module, 0)]
    bench.step(3)
    bench.step()
    assert (bench.frame, bench.block) == (4, 2)
    assert counter.seen == [(48000.0, float32(1 / 48000), f) for f in range(4)]
    assert [call for f in range(4) for call in (("counter", f), ("doubler1", f))] == CALLS
    # A cable delivers an output to its input after every module has processed: one frame later.
    assert doubler.module.outputs[0].get_channels() == 0  # unconnected
    assert voltages(doubler.module, channels=1) == [4.0]
    bench.set_sample_rate(96000)
    bench.step()
    assert counter.seen[-1] == (96000.0, float32(1 / 96000), 4)


def test_feedback_through_a_cable_has_one_frame_of_delay() -> None:
    bench = Bench()
    seed = Module.create(lambda m: m.config(0, 0, 1, 0))
    a, b = Doubler.create(), Doubler.create()
    for m in (seed, a.module, b.module):
        bench.add(m)
    bench.connect(seed, 0, a.module, 0)
    bench.connect(b.module, 0, a.module, 0)  # stacked with the seed
    bench.connect(a.module, 0, b.module, 0)
    seed.outputs[0].set_voltage(1.0)
    history = []
    for _ in range(6):
        bench.step()
        history.append((voltages(a.module)[0], voltages(b.module)[0]))
    # a(n) = 2 * (1 + b(n-1)); b(n) = 2 * a(n-1); inputs start at 0 V.
    assert history == [(0.0, 0.0), (2.0, 0.0), (2.0, 4.0), (10.0, 4.0), (10.0, 20.0), (42.0, 20.0)]


def test_cables_stack_sum_polyphony_and_replace_non_finite_voltages() -> None:
    bench = Bench()
    poly = Module.create(lambda m: m.config(0, 0, 2, 0))
    target = Doubler.create()
    bench.add(poly)
    bench.add(target.module)
    first = bench.connect(poly, 0, target.module, 0)
    bench.connect(poly, 1, target.module, 0)
    poly.outputs[0].set_channels(3)
    for c, v in enumerate([1.0, math.inf, -2.0]):
        poly.outputs[0].set_voltage(v, c)
    poly.outputs[1].set_voltage(0.5)  # monophonic: added to every channel
    bench.step(2)
    assert target.module.inputs[0].get_channels() == 3
    assert voltages(target.module, channels=3) == [3.0, 1.0, -3.0]
    bench.disconnect(first)  # one cable left: copies its channels
    poly.outputs[1].set_voltage(math.nan)
    bench.step(2)
    assert target.module.inputs[0].get_channels() == 1
    assert voltages(target.module, channels=1) == [0.0]
    with pytest.raises(ValueError, match="no cable"):
        bench.disconnect(first)


def test_bypass_runs_process_bypass_with_racks_routes_or_an_override() -> None:
    bench = Bench()
    seed = Module.create(lambda m: m.config(0, 0, 1, 0))

    class Routed(Doubler):
        def configure(self) -> None:
            super().configure()
            self.module.config_bypass(0, 0)

    class Silent(Doubler):
        def process_bypass(self, args: ProcessArgs) -> None:
            self.module.outputs[0].set_voltage(-1.0)

    routed, silent = Routed.create(), Silent.create()
    sink = Module.create(lambda m: m.config(0, 2, 0, 0))  # Rack keeps an unconnected output at 0 channels
    for m in (seed, routed.module, silent.module, sink):
        bench.add(m)
    bench.connect(seed, 0, routed.module, 0)
    bench.connect(seed, 0, silent.module, 0)
    bench.connect(routed.module, 0, sink, 0)
    bench.connect(silent.module, 0, sink, 1)
    seed.outputs[0].set_voltage(3.0)
    bench.step(2)
    assert voltages(routed.module) == [6.0] and voltages(silent.module) == [6.0]
    bench.set_bypassed(routed.module, True)
    bench.set_bypassed(silent.module, True)
    assert bench.is_bypassed(routed.module) and not routed.module.is_bypassed()  # Rack's flag is the engine's
    assert routed.module.outputs[0].get_voltage() == 0.0  # Rack clears the outputs
    bench.step()
    assert voltages(routed.module) == [3.0] and voltages(silent.module) == [-1.0]
    bench.set_bypassed(routed.module, False)
    bench.step()
    assert voltages(routed.module) == [6.0]


def test_superclass_calls_reach_racks_implementations() -> None:
    events: list[str] = []
    with pytest.warns(DeprecationWarning, match="step"):

        class Legacy(ModuleClass):
            def configure(self) -> None:
                self.module.config(0, 0, 1, 0)

            def step(self) -> None:
                events.append("step")
                self.module.outputs[0].set_voltage(7.0)

    class Wrapped(Legacy):
        def process(self, args: ProcessArgs) -> None:
            events.append("process")
            super().process(args)  # Rack's Module::process calls step()

    class Plain(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 0, 0, 0)

    bench = Bench()
    legacy, wrapped, plain = Legacy.create(), Wrapped.create(), Plain.create()
    for m in (legacy.module, wrapped.module, plain.module):
        bench.add(m)
    bench.step()
    assert events == ["step", "process", "step"]
    assert voltages(legacy.module) == []  # unconnected outputs keep 0 channels
    assert legacy.module.outputs[0].get_voltage() == 7.0
    with pytest.warns(DeprecationWarning, match="deprecated in Rack"):
        ModuleClass.step(plain)
    args = ProcessArgs(44100, 1 / 44100, 9)
    assert repr(args).startswith("Module.ProcessArgs(sample_rate=44100") and args.frame == 9
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        ModuleClass.process(plain, args)  # Rack's base: calls step(), which does nothing


def test_a_python_module_matches_the_native_gain_kernel() -> None:
    gain = float32(0.37)  # the kernel's float: then one rounding, as in C++

    class PythonGain(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 1, 1, 0)

        def process(self, args: ProcessArgs) -> None:
            inp, out = self.module.inputs[0], self.module.outputs[0]
            out.set_channels(inp.get_channels())
            for c in range(inp.get_channels()):
                out.set_voltage(inp.get_voltage(c) * gain, c)

    def native(m: Module) -> None:
        m.config(0, 1, 1, 0)
        m.set_kernel(kernel.Gain(input=0, output=0, gain=0.37))

    bench = Bench()
    source = Module.create(lambda m: m.config(0, 0, 1, 0))
    py_gain, c_gain = PythonGain.create(), Module.create(native)
    for m in (source, py_gain.module, c_gain):
        bench.add(m)
    bench.connect(source, 0, py_gain.module, 0)
    bench.connect(source, 0, c_gain, 0)
    source.outputs[0].set_channels(4)
    for frame in range(32):
        for c in range(4):
            source.outputs[0].set_voltage(math.sin(frame * 0.3 + c) * 10 ** (c - 2), c)
        bench.step()
        assert voltages(py_gain.module) == voltages(c_gain)


def test_an_override_error_stops_the_frame_and_reaches_the_caller() -> None:
    order: list[str] = []

    class Fails(Doubler):
        raised = False

        def process(self, args: ProcessArgs) -> None:
            order.append("fails")
            if args.frame == 1 and not self.raised:
                self.raised = True
                raise KeyError("frame one")
            super().process(args)

    class After(Counter):
        def process(self, args: ProcessArgs) -> None:
            order.append("after")
            super().process(args)

    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)  # it overrides step()

        class FailsInStep(ModuleClass):
            def configure(self) -> None:
                self.module.config(0, 0, 0, 0)

            def process(self, args: ProcessArgs) -> None:
                with pytest.raises(ZeroDivisionError):
                    super().process(args)  # the error in step() reaches this caller
                raise ValueError("after step")

            def step(self) -> None:
                raise ZeroDivisionError("in step")

    bench = Bench()
    fails, after = Fails.create(), After.create()
    bench.add(fails.module)
    bench.add(after.module)
    bench.step()
    with pytest.raises(KeyError, match="frame one"):
        bench.step()
    assert order == ["fails", "after", "fails"]  # the module after the error did not run
    assert bench.frame == 1  # the failed frame did not complete
    bench.step()
    assert bench.frame == 2 and after.seen[-1][2] == 1
    raised = _rackpy_host.dispatch_stats()["raised"]
    failing = FailsInStep.create()
    other = Bench()
    other.add(failing.module)
    with pytest.raises(ValueError, match="after step"):
        other.step()
    assert _rackpy_host.dispatch_stats()["raised"] == raised  # raised to the caller, not contained


def test_recursion_through_racks_code_raises_recursion_error() -> None:
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)

        class Recursive(ModuleClass):
            def configure(self) -> None:
                self.module.config(0, 0, 0, 0)

            def process(self, args: ProcessArgs) -> None:
                super().process(args)  # Rack's process calls step()

            def step(self) -> None:
                self.process(ProcessArgs())  # and round again, through C++ each time

    bench = Bench()
    bench.add(Recursive.create().module)
    with pytest.raises(RecursionError, match="laboratory callbacks nested"):
        bench.step()
    assert bench.frame == 0  # the frame did not complete


def test_callbacks_cannot_step_or_edit_a_bench() -> None:
    bench = Bench()
    spare = Module.create(lambda m: m.config(0, 1, 1, 0))
    outcomes: list[str] = []

    class Meddler(ModuleClass):
        def configure(self) -> None:
            self.module.config(0, 1, 1, 0)

        def process(self, args: ProcessArgs) -> None:
            edits: list[Any] = [
                bench.step,
                lambda: Bench().step(),
                lambda: bench.add(spare),
                lambda: bench.remove(self.module),
                lambda: bench.connect(self.module, 0, self.module, 0),
                lambda: bench.set_sample_rate(1000),
                lambda: bench.set_bypassed(self.module, True),
                bench.clear,
            ]
            for edit in edits:
                try:
                    edit()
                except LaboratoryError as e:
                    outcomes.append(str(e).split(" inside")[0])

    meddler = Meddler.create()
    bench.add(meddler.module)
    bench.step()
    assert outcomes == [
        "stepping a bench",
        "stepping a bench",
        "adding a module to a bench",
        "removing a module from a bench",
        "connecting modules on a bench",
        "changing a bench's sample rate",
        "bypassing a module on a bench",
        "clearing a bench",
    ]
    assert bench.modules == (meddler.module,) and bench.frame == 1


def test_bench_ownership_rules() -> None:
    bench = Bench()
    lab = Doubler.create()
    ordinary = Module.create(lambda m: m.config(0, 1, 1, 0))
    assert _rackpy_host.laboratory.is_laboratory_module(lab.module)
    assert not _rackpy_host.laboratory.is_laboratory_module(ordinary)
    with pytest.raises(LaboratoryError, match="audio is its Python process"):
        lab.module.set_kernel(kernel.Gain(input=0, output=0))
    bench.add(lab.module)
    bench.add(ordinary)
    with pytest.raises(OwnershipError, match="already on a laboratory Bench"):
        Bench().add(ordinary)
    with pytest.raises(OwnershipError, match="remove the module from its laboratory Bench"):
        ordinary.close()
    other = Module.create(lambda m: m.config(0, 1, 1, 0))
    with pytest.raises(OwnershipError, match="not on this bench"):
        bench.connect(other, 0, ordinary, 0)
    with pytest.raises(IndexError):
        bench.connect(lab.module, 3, ordinary, 0)
    cable = bench.connect(lab.module, 0, ordinary, 0)
    with pytest.raises(OwnershipError, match="cables are still connected"):
        bench.remove(ordinary)
    bench.disconnect(cable)
    bench.remove(ordinary)
    ordinary.close()
    with pytest.raises(TypeError):
        bench.add(lab)  # type: ignore[arg-type]

    def inside(m: Module) -> None:
        m.config(0, 0, 0, 0)
        with pytest.raises(OwnershipError, match="constructor callback"):
            bench.add(m)

    Module.create(inside)


def test_bench_events_follow_racks_engine() -> None:
    def kinds(m: Module) -> list[str]:
        return [type(event).__name__ for _, event in m.lifecycle_events()]

    bench = Bench(48000)
    a = Module.create(lambda m: m.config(0, 1, 1, 0))
    b = Module.create(lambda m: m.config(0, 1, 1, 0))
    bench.add(a)
    bench.add(b)
    assert kinds(a) == ["AddEvent", "SampleRateChangeEvent"]
    first = bench.connect(a, 0, b, 0)
    second = bench.connect(a, 0, b, 0)  # both ports were connected already: no events
    bench.set_bypassed(a, True)
    bench.set_bypassed(a, True)  # unchanged: nothing
    bench.set_bypassed(a, False)
    bench.set_sample_rate(48000)  # unchanged: nothing
    bench.set_sample_rate(96000)
    bench.disconnect(second)
    bench.disconnect(first)
    assert kinds(a) == ["PortChangeEvent", "BypassEvent", "UnBypassEvent", "SampleRateChangeEvent", "PortChangeEvent"]
    assert kinds(b) == [
        "AddEvent",
        "SampleRateChangeEvent",
        "PortChangeEvent",
        "SampleRateChangeEvent",
        "PortChangeEvent",
    ]
    bench.clear()
    assert bench.modules == () and kinds(a) == ["RemoveEvent"] and kinds(b) == ["RemoveEvent"]


def test_benches_and_laboratory_modules_are_garbage_collected() -> None:
    gc.collect()
    references = _rackpy_host.native_references()

    holder: list[Bench] = [Bench()]

    class Cyclic(Doubler):
        def configure(self) -> None:
            super().configure()
            self.bench = holder[0]  # module -> overrides -> instance -> bench -> module

    cyclic = Cyclic.create()
    holder[0].add(cyclic.module)
    holder[0].step()
    del cyclic
    holder.clear()
    gc.collect()
    assert _rackpy_host.native_references() == references
