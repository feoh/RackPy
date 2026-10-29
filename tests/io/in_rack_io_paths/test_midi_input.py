# SPDX-License-Identifier: GPL-3.0-or-later
"""Native frame-timed MIDI input routed to a production kernel inside Rack."""

import gc
import importlib
from typing import Any

import pytest

from rackpy import raw
from rackpy.errors import OwnershipError
from rackpy.raw import midi
from rackpy.raw.engine import Module

runner: Any = importlib.import_module("rackpy_in_rack_pytest")


def note(status: int, frame: int) -> midi.Message:
    message = midi.Message()
    message.bytes = [status, 60, 100 if status == 0x90 else 0]
    message.frame = frame
    return message


def test_attachment_guards_and_reattachment() -> None:
    module = Module.create(lambda m: m.config(0, 0, 0, 0))
    other = Module.create(lambda m: m.config(0, 0, 0, 0))
    queue = midi.InputQueue()
    with pytest.raises(ValueError, match="index"):
        module.set_midi_input(4, queue)
    with pytest.raises(TypeError, match="InputQueue"):
        module.set_midi_input(0, midi.Input())  # type: ignore[arg-type]
    queue.on_message(note(0x90, 1))
    with pytest.raises(OwnershipError, match="empty"):
        module.set_midi_input(0, queue)
    assert queue.try_pop(1) is not None
    queue.set_driver_id(-12)
    queue.set_device_id(0)
    with pytest.raises(OwnershipError, match="disconnect"):
        module.set_midi_input(0, queue)
    queue.set_device_id(-1)
    module.set_midi_input(0, queue)
    with pytest.raises(OwnershipError, match="already attached"):
        other.set_midi_input(0, queue)
    module.set_midi_input(0, None)
    other.set_midi_input(0, queue)
    other.set_midi_input(0, None)
    module.close()
    other.close()


def test_dropping_the_input_before_the_module_is_safe() -> None:
    box = [midi.InputQueue()]

    def configure(m: Module) -> None:
        m.config(0, 0, 0, 0)
        m.set_midi_input(0, box[0])

    module = Module.create(configure)
    box.clear()
    gc.collect()
    stats = module.midi_input_stats(0)
    assert stats is not None and stats["received"] == 0
    module.set_midi_input(0, None)
    assert module.midi_input_stats(0) is None
    module.close()


def test_native_midi_input_delivers_messages_at_their_engine_frame() -> None:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    engine = context.engine
    model = raw.plugin.get_model("RackPy", "MidiProbe")
    assert model is not None
    queue = midi.InputQueue()
    output = midi.Output()
    runner.midi_input = queue
    try:
        module = model.create_module()
    finally:
        runner.midi_input = None
    assert isinstance(module, Module)
    for port in (queue, output):
        port.set_driver_id(-12)  # Rack's Loopback driver
        port.set_device_id(0)
    engine.add_module(module)
    previous = engine.get_master_module()
    engine.set_master_module(module)
    try:
        with pytest.raises(OwnershipError, match="only while Python owns"):
            module.set_midi_input(0, None)
        frame = engine.get_frame()
        with pytest.raises(OwnershipError, match="consumed by its native kernel"):
            queue.try_pop(frame + 100)
        with pytest.raises(OwnershipError, match="consumed by its native kernel"):
            queue.size()
        queue.on_message(note(0x90, frame + 3))
        output.send_message(note(0x80, frame + 7))  # Rack's device delivers the second message
        engine.step_block(3)
        assert module.outputs[1].get_voltage() == 0.0
        engine.step_block(1)
        assert module.outputs[1].get_voltage() == 10.0
        assert module.outputs[0].get_voltage() == pytest.approx(0.0)  # MIDI C4
        engine.step_block(3)
        assert module.outputs[1].get_voltage() == 10.0
        engine.step_block(1)
        assert module.outputs[1].get_voltage() == 0.0
        stats = module.midi_input_stats(0)
        assert stats is not None
        assert stats["received"] == stats["delivered"] == 2
        assert stats["dropped_full"] == stats["dropped_long"] == 0
    finally:
        engine.set_master_module(previous)
        engine.remove_module(module)
        module.set_midi_input(0, None)
        assert module.midi_input_stats(0) is None
        queue.on_message(note(0x90, 1))
        assert queue.try_pop(1) is not None  # normal Rack queue behavior resumes
        queue.set_device_id(-1)
        output.set_device_id(-1)
