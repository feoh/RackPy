# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-17A: native kernels driven by Rack's own engine (live UI suite)."""

import gc
import math
import threading
import time
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.dsp.kernel import CONTROL_CAPACITY, Gain, Kernel, Sine, kernel_types, load
from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.plugin import ModuleClass
from rackpy.raw.engine import Module

Probe = load("rackpy_test.Probe")


def until(condition: Callable[[], bool], seconds: float = 2.0) -> bool:
    """Rack's engine runs on its own thread; give it time to process frames."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return True
        time.sleep(0.005)
    return condition()


def alive(kernel: Kernel) -> bool:
    return kernel.alive


def bypass_of(m: Module) -> Kernel | None:
    return m.bypass_kernel  # a call, so that type checkers do not narrow it between Rack calls


def adopted(configure: Callable[[Module], object]) -> Module:
    m = Module.create(configure)
    HOOKS.engine_adopt(m)
    return m


def test_kernel_types_describe_themselves() -> None:
    assert {t.name for t in kernel_types()} >= {"rackpy.Gain", "rackpy.Sine", "rackpy_test.Probe"}
    assert load("rackpy.Gain") is Gain and Gain.controls == ("gain",)
    assert [f[:4] for f in Gain.fields] == [
        ("input", "input", 0.0, True),
        ("output", "output", 0.0, True),
        ("param", "param", -1.0, False),
        ("gain", "number", 1.0, False),
    ]
    k = Gain(input=0, output=0, gain=2)
    assert isinstance(k, Kernel) and not k.attached and k.alive and k.type is Gain
    assert k.values == {"input": 0, "output": 0, "param": -1, "gain": 2.0}
    k.set("gain", 3.5)
    assert k.values["gain"] == 3.5 and repr(k) == "<kernel rackpy.Gain (not attached)>"
    with pytest.raises(KeyError):
        load("rackpy.Missing")


@pytest.mark.parametrize(
    ("fields", "error", "message"),
    [
        ({"output": 0}, TypeError, "needs input"),
        ({"input": 0, "output": 0, "volume": 1}, TypeError, "unexpected field volume"),
        ({"input": True, "output": 0}, TypeError, "not a bool"),
        ({"input": 0.0, "output": 0}, TypeError, "index"),
        ({"input": 0, "output": 0, "gain": math.inf}, ValueError, "finite"),
        ({"input": -2, "output": 0}, ValueError, "out of range"),
    ],
)
def test_bad_fields_are_refused(fields: dict[str, Any], error: type[Exception], message: str) -> None:
    with pytest.raises(error, match=message):
        Gain(**fields)


def test_wiring_is_checked_when_the_kernel_is_set() -> None:
    def build(m: Module) -> None:
        m.config(1, 1, 1, 0)
        with pytest.raises(ValueError, match=r"rackpy.Gain.output = 3, but the module has 1 outputs"):
            m.set_kernel(Gain(input=0, output=3))
        with pytest.raises(ValueError, match="must name one of the module's inputs"):
            m.set_kernel(Gain(input=-1, output=0))
        k = Gain(input=0, output=0, param=0)
        m.set_kernel(k)
        assert m.kernel is k and k.attached
        with pytest.raises(OwnershipError, match="already belongs"):
            m.set_kernel(k)
        with pytest.raises(TypeError, match="takes a kernel"):
            m.set_kernel("Gain")  # type: ignore[arg-type]

    m = Module.create(build)
    try:
        assert m.kernel is not None and m.kernel.type is Gain and not m.kernel_failed
    finally:
        m.close()


def test_rack_runs_kernels_and_controls_reach_them() -> None:
    source = adopted(lambda m: m.config(0, 0, 1, 0))  # its output is set from Python: no kernel writes it

    def vca(m: Module) -> None:
        m.config(1, 1, 1, 0)
        m.config_param(0, 0, 2, 0.5, "Gain")
        m.set_kernel(Gain(input=0, output=0, param=0, gain=1))

    gain = adopted(vca)
    cable = HOOKS.engine_connect(source, 0, gain, 0)
    try:
        source.outputs[0].set_voltage(2.0)
        out = gain.outputs[0]
        assert until(lambda: out.get_voltage() == 1.0)  # 2 V * param 0.5 * gain 1
        gain.params[0].value = 1.5
        assert until(lambda: out.get_voltage() == 3.0)
        kernel = gain.kernel
        assert kernel is not None
        kernel.set("gain", -2)
        assert until(lambda: out.get_voltage() == -6.0)  # through the control queue, at a frame boundary
        assert kernel.values["gain"] == -2.0
    finally:
        HOOKS.engine_disconnect(cable)
        HOOKS.engine_delete(gain, None)
        HOOKS.engine_delete(source, None)


def test_sine_runs_polyphonic_and_its_amplitude_is_a_control() -> None:
    def osc(m: Module) -> None:
        m.config(1, 0, 1, 0)
        m.config_param(0, -2, 2, 0, "Pitch")
        m.set_kernel(Sine(output=0, pitch_param=0, amplitude=5))

    m = adopted(osc)
    try:
        out = m.outputs[0]
        samples: list[float] = []

        def loud() -> bool:
            samples.append(abs(out.get_voltage()))
            return max(samples) > 4.0

        assert until(loud)
        kernel = m.kernel
        assert kernel is not None
        kernel.set("amplitude", 1.0)
        time.sleep(0.05)
        late = []
        for _ in range(50):  # Rack's engine renders in blocks: sample across several of them
            late.append(out.get_voltage())
            time.sleep(0.002)
        assert max(map(abs, late)) <= 1.0 + 1e-6 and len(set(late)) > 1  # still running, quieter
    finally:
        HOOKS.engine_delete(m, None)


def test_reset_rate_bypass_and_failure_are_native() -> None:
    def probed(m: Module) -> None:
        m.config(0, 0, 3, 0)
        m.set_kernel(Probe())

    m = adopted(probed)
    frames, rate, resets = (m.outputs[i] for i in range(3))
    try:
        assert until(lambda: frames.get_voltage() > 100)
        HOOKS.engine_set_sample_rate(96000)
        # Rack's onSampleRateChange reached the kernel (the SDK's fast-math division rounds 96000/1000)
        assert until(lambda: abs(rate.get_voltage() - 96.0) < 1e-3), rate.get_voltage()
        HOOKS.engine_reset(m)
        assert until(lambda: resets.get_voltage() == 1.0)
        HOOKS.engine_bypass(m, True)  # Rack calls processBypass, not the kernel
        time.sleep(0.05)
        held = frames.get_voltage()
        time.sleep(0.05)
        assert frames.get_voltage() == held
        HOOKS.engine_bypass(m, False)
        assert until(lambda: frames.get_voltage() > held)
        kernel = m.kernel
        assert kernel is not None
        sequence = _rackpy_host.diagnostics_recorded()
        kernel.set("fail_after", frames.get_voltage() + 10)
        assert until(lambda: m.kernel_failed)
        assert until(lambda: all(o.get_voltage() == 0.0 for o in (frames, rate, resets)))  # silenced
        assert any(
            code == "kernel_failed" and "rackpy_test.Probe" in message
            for _, _, code, _, message in _rackpy_host.diagnostics(sequence)
        )
        kernel.set("fail_after", 0)  # controls still reach a failed kernel
        HOOKS.engine_reset(m)  # Initialize clears the failure
        assert until(lambda: not m.kernel_failed and frames.get_voltage() > 0)
    finally:
        HOOKS.engine_set_sample_rate(48000)
        HOOKS.engine_delete(m, None)


def test_replacement_under_load_and_teardown() -> None:
    m = adopted(lambda m: m.config(1, 1, 2, 0))
    kernels: list[Kernel] = []
    try:
        for i in range(300):
            k = Gain(input=0, output=0) if i % 2 else Sine(output=1, pitch_input=0)
            m.set_kernel(k)
            kernels.append(k)
            if i % 50 == 0:
                time.sleep(0.002)  # let the engine pick some up
        last = kernels[-1]
        assert m.kernel is last and last.alive
        assert not kernels[0].alive  # replaced long ago and reclaimed
        with pytest.raises(StaleHandleError, match="replaced"):
            kernels[-2].set("amplitude" if kernels[-2].type is Sine else "gain", 1)
        assert until(lambda: m.outputs[1].get_voltage() != 0.0 or m.kernel is last)
        m.set_kernel(Sine(output=1))  # still waiting when the module goes away
    finally:
        HOOKS.engine_delete(m, None)
    gc.collect()
    assert not alive(last)
    with pytest.raises(StaleHandleError, match="deleted"):
        last.set("gain", 1)


def test_python_owned_modules_apply_changes_at_once() -> None:
    m = Module.create(lambda m: m.config(0, 0, 3, 0))
    try:
        first, second = Probe(), Probe()
        m.set_kernel(first)
        m.set_kernel(second)  # no engine thread: the first is deleted now
        assert not first.alive and second.alive and m.kernel is second
        for _ in range(CONTROL_CAPACITY + 10):
            second.set("fail_after", 0)  # applied directly, so nothing queues up
    finally:
        m.close()
    assert not second.alive


def test_module_class_kernels_and_thread_rules() -> None:
    class Vca(ModuleClass):
        def configure(self) -> None:
            self.module.config(1, 1, 1, 0)
            self.module.config_param(0, 0, 1, 1, "Level")
            self.module.set_kernel(Gain(input=0, output=0, param=0))

    m = Module.create(Vca.factory())
    try:
        kernel = m.kernel
        assert kernel is not None and kernel.type is Gain
        errors: list[BaseException] = []

        def worker() -> None:
            actions: list[Callable[[], object]] = [
                lambda: m.set_kernel(Gain(input=0, output=0)),
                lambda: kernel.set("gain", 2),
                lambda: Sine(output=0),
            ]
            for action in actions:
                try:
                    action()
                except BaseException as error:
                    errors.append(error)

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(errors) == 3 and all(isinstance(e, ThreadError) for e in errors)
    finally:
        m.close()


def test_a_bypass_kernel_is_racks_process_bypass() -> None:
    """RP-34: while the module is bypassed Rack's engine runs the bypass kernel; without one, the routes."""
    from rackpy.dsp.graph import Graph

    normal, bypassed = Graph(), Graph()
    normal.output(0, normal.constant(3.0))
    bypassed.output(0, -bypassed.input(0))  # custom bypass semantics: the input, inverted

    def configure(m: Module) -> None:
        m.config(0, 1, 1, 0)
        m.config_bypass(0, 0)
        m.set_kernel(normal.compile())

    feed = Graph()
    feed.output(0, feed.constant(2.0))

    def source_module(m: Module) -> None:
        m.config(0, 0, 1, 0)
        m.set_kernel(feed.compile())

    source = adopted(source_module)
    m = adopted(configure)
    sink = adopted(lambda m: m.config(0, 1, 0, 0))
    cables = [HOOKS.engine_connect(source, 0, m, 0), HOOKS.engine_connect(m, 0, sink, 0)]
    out = m.outputs[0]
    try:
        assert bypass_of(m) is None and not m.bypass_kernel_failed
        assert until(lambda: out.get_voltage() == 3.0)
        HOOKS.engine_bypass(m, True)
        assert until(lambda: out.get_voltage() == 2.0)  # no bypass kernel: Rack copies the route
        kernel = bypassed.compile()
        m.set_bypass_kernel(kernel)
        assert bypass_of(m) is kernel and m.kernel is not kernel
        assert until(lambda: out.get_voltage() == -2.0)  # picked up at a frame boundary, no Python
        HOOKS.engine_bypass(m, False)
        assert until(lambda: out.get_voltage() == 3.0)
        with pytest.raises(OwnershipError, match="already belongs"):
            m.set_bypass_kernel(kernel)
        with pytest.raises(OwnershipError, match="already belongs"):
            m.set_kernel(kernel)
        with pytest.raises(TypeError, match="set_bypass_kernel takes a kernel"):
            m.set_bypass_kernel(object())  # type: ignore[arg-type]
    finally:
        for cable in cables:
            HOOKS.engine_disconnect(cable)
        for module in (sink, m, source):
            HOOKS.engine_delete(module, None)
    assert not alive(kernel)  # deleted with its module
