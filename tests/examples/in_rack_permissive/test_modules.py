# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-52 actual-Rack tests for the envelope and governed feedback kernels."""

import pytest
import rackpyenvelope_reference as envelope_reference
from _rackpy_host import _testing as HOOKS
from rackpyfeedbackgovernor.reference import FeedbackGovernorReference

from rackpy import raw
from rackpy.raw.engine import Module


def make(model_name: str) -> Module:
    # Both modules are registered into RackPy's single in-host plugin package.
    model = raw.plugin.get_model("RackPy", model_name)
    assert model is not None
    module = model.create_module()
    assert isinstance(module, Module)
    return module


@pytest.mark.parametrize("rate", [44100, 48000, 96000])
def test_envelope_kernel_matches_frame_reference(rate: int) -> None:
    module = make("Envelope")
    reference = envelope_reference.ADReference()
    try:
        module.params[0].value = 0.0
        module.params[1].value = 0.0
        module.inputs[0].set_channels(1)
        assert module.kernel is not None
        outputs = []
        eoc_frames = []
        for frame in range(6000):
            module.inputs[0].set_voltage(10.0 if frame == 1 else 0.0, 0)
            HOOKS.module_process(module, rate, frame)
            expected = reference.process(10.0 if frame == 1 else 0.0, 0.0, 0.0, False, rate)
            actual = tuple(module.outputs[index].get_voltage(0) for index in range(3))
            assert actual[:2] == pytest.approx(expected[:2], abs=2e-4), (frame, actual, expected)
            if actual[2] > 0.0:
                eoc_frames.append(frame)
            outputs.append(actual)
        assert max(row[0] for row in outputs) == pytest.approx(10.0, abs=0.02)
        assert eoc_frames
        pulse_length = eoc_frames[-1] - eoc_frames[0] + 1
        assert abs(pulse_length - round(rate * 0.001)) <= 1
    finally:
        module.close()


def test_envelope_loop_switch_restarts_after_eoc() -> None:
    module = make("Envelope")
    try:
        module.params[0].value = 0.0
        module.params[1].value = 0.0
        module.params[2].value = 1.0
        module.inputs[0].set_channels(1)
        assert module.kernel is not None
        peaks = 0
        was_peak = False
        for frame in range(10000):
            module.inputs[0].set_voltage(10.0 if frame == 1 else 0.0, 0)
            HOOKS.module_process(module, 48000, frame)
            peak = module.outputs[0].get_voltage(0) >= 9.99
            peaks += int(peak and not was_peak)
            was_peak = peak
        assert peaks >= 4
    finally:
        module.close()


def test_feedback_kernel_matches_reference_and_stays_within_cap() -> None:
    module = make("FeedbackGovernor")
    reference = FeedbackGovernorReference()
    try:
        module.params[0].value = 0.7
        module.params[1].value = 3.0
        module.params[2].value = 5000.0
        module.params[3].value = 0.8
        for port in module.inputs:
            port.set_channels(1)
        for frame in range(4000):
            send = 1.0 if frame % 100 < 50 else -1.0
            returned = 4.0 if frame < 2000 else 2.0
            cv = 1.0 if frame % 2 else 0.0
            module.inputs[0].set_voltage(send, 0)
            module.inputs[1].set_voltage(returned, 0)
            module.inputs[2].set_voltage(cv, 0)
            module.inputs[3].set_voltage(0.0, 0)
            HOOKS.module_process(module, 48000, frame)
            expected = reference.process(
                send, returned, feedback=0.7, cap=3.0, tone=5000.0, decay=0.8, cv=cv, sample_rate=48000
            )
            actual = module.outputs[0].get_voltage(0)
            assert actual == pytest.approx(expected, abs=2e-5)
            assert abs(actual) <= 3.0
    finally:
        module.close()


def test_feedback_kill_button_and_gate_clear_the_return() -> None:
    module = make("FeedbackGovernor")
    try:
        module.inputs[1].set_channels(1)
        module.inputs[1].set_voltage(10.0, 0)
        for frame in range(100):
            HOOKS.module_process(module, 48000, frame)
        module.inputs[3].set_channels(1)
        module.inputs[3].set_voltage(10.0, 0)
        HOOKS.module_process(module, 48000, 100)
        assert module.outputs[0].get_voltage(0) == 0.0
    finally:
        module.close()
