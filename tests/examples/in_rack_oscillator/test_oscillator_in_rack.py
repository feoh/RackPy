# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-33: examples/oscillator's module, made from its model inside Rack's live UI.

The module is stepped frame by frame on the main thread (``module_process``) through
the fixed input streams and compared with the C++ reference; then it runs in Rack's
own engine. The plugin's entry setup imported the example, which added the model.
"""

import time
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import raw
from rackpy.raw.engine import Module
from tests.examples.signal import FREQ_C4, check_blink, check_parity, check_sine, stream

RATES = (44100, 48000, 96000)


def make() -> Module:
    model = raw.plugin.get_model("RackPy", "Oscillator")
    assert model is not None
    module = model.create_module()
    assert isinstance(module, Module)
    return module


def step(module: Module, rate: float, knob: list[float], voct: list[float] | None, first: int = 0) -> Any:
    """Steps the frames; returns the sine's channel 0 and the light, per frame."""
    sine: list[float] = []
    lights: list[float] = []
    port = module.inputs[0]
    port.channels = 0 if voct is None else 1
    last_knob = last_volts = None
    for i, value in enumerate(knob):
        if value != last_knob:
            module.params[0].value = last_knob = value
        if voct is not None and voct[i] != last_volts:
            port.set_voltage(last_volts := voct[i], 0)
        HOOKS.module_process(module, rate, first + i)
        sine.append(module.outputs[0].get_voltage(0))
        lights.append(module.lights[0].get_brightness())
    return sine, lights


def test_the_models_controls() -> None:
    m = make()
    try:
        q = m.get_param_quantity(0)
        assert (q.name, q.unit, q.get_min_value(), q.get_max_value(), q.get_default_value()) == (
            "Frequency",
            " Hz",
            -3.0,
            3.0,
            0.0,
        )
        assert q.get_display_value() == pytest.approx(FREQ_C4, rel=1e-6)
        m.params[0].value = 1.0
        assert q.get_display_value() == pytest.approx(2 * FREQ_C4, rel=1e-6)  # an octave per volt
        assert m.get_input_info(0).name == "1V/octave pitch" and m.get_output_info(0).name == "Sine"
        light = m.get_light_info(0)
        assert light is not None and light.name == "Blink"
        assert m.kernel is not None
    finally:
        m.close()


@pytest.mark.parametrize("kind", ["unpatched", "patched"])
@pytest.mark.parametrize("rate", RATES)
def test_the_module_matches_the_cpp_reference(reference: dict[str, Any], rate: int, kind: str) -> None:
    knob, voct = stream(rate, kind)
    m = make()
    try:
        sine, lights = step(m, rate, knob, voct)
    finally:
        m.close()
    check_sine(sine, rate, knob, voct)
    check_blink(lights, rate)
    worst = check_parity(sine, lights, [tuple(f) for f in reference[f"{rate}/{kind}"]])
    print(f"{rate} {kind}: at most {worst:.6f} V from the C++ reference")


def test_initialize_restarts_the_oscillator() -> None:
    fresh, used = make(), make()
    try:
        first, _ = step(fresh, 48000, [0.0] * 4, None)
        step(used, 48000, [1.0] * 1000, None)
        used.on_reset(Module.ResetEvent())  # Initialize: Rack resets the knob, RackPy the kernel's phases
        assert used.params[0].value == 0.0
        again, lights = step(used, 48000, [0.0] * 4, None, first=1000)
        assert again == first and lights == [1.0] * 4
    finally:
        fresh.close()
        used.close()


def test_a_sample_rate_change_keeps_the_waveform_continuous() -> None:
    m = make()
    try:
        before, _ = step(m, 44100, [0.0] * 4410, None)
        after, _ = step(m, 96000, [0.0] * 9600, None, first=4410)
    finally:
        m.close()
    jump = abs(after[0] - before[-1])
    assert jump <= 2 * 3.14159266 * 5 * FREQ_C4 / 44100  # no step larger than the slower rate's slope
    check_sine(after, 96000, [0.0] * len(after), None)


def test_the_browser_preview_has_no_module() -> None:
    width, height, labels, has_module, same_model = HOOKS.model_preview("RackPy", "Oscillator")
    assert width == pytest.approx(120.0, abs=0.01)  # 8 HP
    assert (height, has_module, same_model) == (380.0, False, True)
    assert labels == []


def test_it_runs_in_racks_engine_and_over_a_cable() -> None:
    source, sink = make(), make()
    HOOKS.engine_adopt(source)
    HOOKS.engine_adopt(sink)
    cable = None
    try:
        assert source.outputs[0].get_channels() == 0  # Rack keeps an unpatched output at 0 channels
        cable = HOOKS.engine_connect(source, 0, sink, 0)
        seen: set[float] = set()
        deadline = time.monotonic() + 2.0
        while len(seen) < 20 and time.monotonic() < deadline:
            seen.add(sink.inputs[0].get_voltage(0))
            time.sleep(0.002)
        assert len(seen) >= 20 and all(-5.0 <= v <= 5.0 for v in seen)
        assert source.outputs[0].get_channels() == 1 and sink.inputs[0].get_channels() == 1
    finally:
        if cable is not None:
            HOOKS.engine_disconnect(cable)
        HOOKS.engine_delete(sink, None)
        HOOKS.engine_delete(source, None)
