# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29A: rack::midi in Rack's live UI, with Rack's Loopback MIDI driver (-12): an output on loopback
device N reaches the inputs on loopback device N."""

from collections.abc import Iterator

import pytest

from rackpy import raw
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import midi

LOOPBACK = -12


def note_on(channel: int, note: int, velocity: int) -> midi.Message:
    message = midi.Message()
    message.set_status(0x9)
    message.set_channel(channel)
    message.set_note(note)
    message.set_value(velocity)
    return message


def alive(view: midi.PortView) -> bool:
    return view.alive  # a call, so that type checkers do not narrow it between Rack calls


def engine_frame() -> int:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine.get_frame()


@pytest.fixture
def loop() -> Iterator[tuple[midi.Output, midi.InputQueue]]:
    out, queue = midi.Output(), midi.InputQueue()
    for port in (out, queue):
        port.set_driver_id(LOOPBACK)
        port.set_device_id(0)
    yield out, queue
    for port in (out, queue):
        port.set_device_id(-1)


def test_messages() -> None:
    message = midi.Message()
    assert message.bytes == [0, 0, 0] and message.frame == -1 and message.get_size() == 3
    message = note_on(3, 200, 100)  # Rack masks data bytes to 7 bits
    assert message.bytes == [0x93, 200 & 0x7F, 100] and message.to_string() == "93 48 64"
    assert (message.get_status(), message.get_channel(), message.get_note(), message.get_value()) == (9, 3, 72, 100)
    message.set_frame(42)
    assert message.get_frame() == 42 and message.frame == 42
    message.set_size(1)
    assert message.get_note() == 0 and message.get_value() == 0  # Rack's: missing bytes read as 0
    message.set_note(5)  # and setting them does nothing
    assert message.bytes == [0x93]
    with pytest.raises(ValueError, match="0 to 255"):
        message.bytes = [256]
    with pytest.raises(ValueError):
        message.set_size(-1)
    with pytest.raises(TypeError):
        message.set_channel(300)  # uint8_t, as in C++
    copy = midi.Message()
    copy.bytes, copy.frame = [0x93], 42
    assert copy == message


def test_racks_drivers() -> None:
    ids = midi.get_driver_ids()
    assert LOOPBACK in ids and midi.get_driver(123456) is None
    loopback = midi.get_driver(LOOPBACK)
    assert loopback is not None and loopback.id == LOOPBACK and loopback.get_name() == "Loopback"
    assert loopback.get_input_device_ids() == list(range(16)) and loopback.get_default_input_device_id() == 0
    assert loopback.get_input_device_name(2) == "Loopback 3" and loopback.get_output_device_name(0) == "Loopback 1"
    assert loopback == midi.get_driver(LOOPBACK)
    names = {midi.get_driver(i).get_name() for i in ids}  # type: ignore[union-attr]
    assert {"Loopback", "Computer keyboard/mouse", "Gamepad"} <= names  # RtMidi's (ALSA, JACK) depend on the machine


def test_through_the_loopback(loop: tuple[midi.Output, midi.InputQueue]) -> None:
    out, queue = loop
    assert out.get_driver_id() == LOOPBACK and out.get_device_id() == 0 and out.get_channel() == 0
    assert queue.get_channel() == -1 and queue.get_channels() == list(range(-1, 16))
    assert out.get_channel_name(-1) and out.get_device_name(0) == "Loopback 1"
    out.set_channel(4)
    out.send_message(note_on(0, 60, 127))  # the output sets its channel
    assert queue.size() == 1
    got = queue.try_pop(2**62)
    assert got is not None and got.get_channel() == 4 and got.get_note() == 60 and got.frame >= 0
    assert queue.try_pop(2**62) is None
    later = note_on(0, 61, 1)
    later.frame = engine_frame() + 10**9
    out.send_message(later)
    assert queue.try_pop(engine_frame()) is None and queue.size() == 1  # not its frame yet
    assert queue.try_pop(2**62) is not None
    queue.channel = 5  # an input filters channels
    out.send_message(note_on(0, 62, 1))
    assert queue.size() == 0
    queue.on_message(note_on(1, 1, 1))  # Rack's: queues it as the driver does
    assert queue.size() == 1


def test_json_and_devices(loop: tuple[midi.Output, midi.InputQueue]) -> None:
    out, queue = loop
    out.set_channel(2)
    data = out.to_json()
    assert data == {"driver": LOOPBACK, "deviceName": "Loopback 1", "channel": 2}
    other = midi.Output()
    other.from_json(data)
    assert (other.driver_id, other.device_id, other.channel) == (LOOPBACK, 0, 2)
    device = queue.device
    assert isinstance(device, midi.InputDevice) and device.get_name() == "Loopback 1" and device.alive
    assert queue in device.subscribed and isinstance(out.output_device, midi.OutputDevice)
    driver = queue.get_driver()
    assert driver is not None and driver.id == LOOPBACK

    # A second subscription through the driver, then through the device.
    extra = driver.subscribe_input(1, queue)
    assert extra is not None and extra.get_name() == "Loopback 2"
    out.set_device_id(1)
    out.send_message(note_on(0, 70, 1))
    assert queue.size() == 1
    driver.unsubscribe_input(1, queue)
    with pytest.raises(OwnershipError):
        driver.unsubscribe_input(1, queue)
    assert not extra.alive
    with pytest.raises(StaleHandleError):
        extra.get_name()

    simulated = note_on(9, 36, 90)  # as if the device received it
    device.on_message(simulated)
    assert queue.size() == 2
    other.set_device_id(0)
    sent = other.output_device
    assert sent is not None
    sent.send_message(note_on(0, 40, 1))  # Rack's device call: no channel is applied
    assert queue.size() == 3
    other.set_device_id(-1)
    assert not sent.alive and other.device is None


def test_port_getters_and_device_subscriptions(loop: tuple[midi.Output, midi.InputQueue]) -> None:
    out, queue = loop
    queue.set_device_id(2)
    assert queue.get_driver_id() == LOOPBACK and queue.get_device_id() == 2 == queue.device_id
    held = queue.get_device()
    assert held is not None and held.get_name() == "Loopback 3"
    assert queue.input_device is not None and queue.input_device.get_name() == "Loopback 3"  # Rack's Input::inputDevice
    queue.set_device_id(0)
    assert queue.input_device is not None and queue.input_device.get_name() == "Loopback 1"

    listener = midi.InputQueue()  # on another loopback device; subscribes to device 0 as well
    listener.set_driver_id(LOOPBACK)
    listener.set_device_id(5)
    device = queue.input_device
    assert device is not None
    try:
        device.subscribe(listener)
        assert listener in device.subscribed
        out.send_message(note_on(0, 50, 1))  # on loopback device 0
        assert listener.size() == 1 and queue.size() == 1
        device.unsubscribe(listener)
        assert listener not in device.subscribed
        out.send_message(note_on(0, 51, 1))
        assert listener.size() == 1 and queue.size() == 2  # the listener no longer hears device 0
        with pytest.raises(OwnershipError):
            device.unsubscribe(listener)  # not subscribed any more
        with pytest.raises(OwnershipError):
            device.unsubscribe(queue)  # its own device: not a subscription InputDevice.subscribe made
    finally:
        listener.set_device_id(-1)

    sender = midi.Output()
    sender.set_driver_id(LOOPBACK)
    sender.set_device_id(5)
    output_device = out.output_device
    assert output_device is not None and output_device.get_name() == "Loopback 1"
    try:
        output_device.subscribe(sender)
        assert sender in output_device.subscribed
        output_device.unsubscribe(sender)
        assert sender not in output_device.subscribed and out in output_device.subscribed
        with pytest.raises(OwnershipError):
            output_device.unsubscribe(sender)
    finally:
        sender.set_device_id(-1)


def test_ports_need_the_live_context() -> None:
    with pytest.raises(TypeError):
        midi.Port()  # Rack's abstract port


def test_python_overrides_of_racks_port_virtuals() -> None:
    class Third(midi.Input):
        def get_default_device_id(self) -> int:
            return 2

        def get_channels(self) -> list[int]:
            return [0, 1]

    port = Third()
    port.set_driver_id(LOOPBACK)  # Rack's setDriverId asks the (Python) port for its default device
    assert port.get_device_id() == 2 and port.get_channels() == [0, 1]
    port.set_device_id(-1)


def midi_json(device: str) -> dict[str, object]:
    """Core's MIDI-CV module's JSON with its port on a loopback device (Rack's Module::fromJson needs plugin)."""
    return {
        "plugin": "Core",
        "model": "MIDIToCVInterface",
        "data": {"midi": {"driver": LOOPBACK, "deviceName": device}},
    }


def test_ports_rack_made_are_port_views(loop: tuple[midi.Output, midi.InputQueue]) -> None:
    from rackpy.raw.engine import EngineModule

    context = raw.context_get()
    assert context is not None and context.engine is not None
    engine = context.engine
    model = raw.plugin.get_model("Core", "MIDIToCVInterface")
    assert model is not None
    module = model.create_module()
    assert isinstance(module, EngineModule)
    engine.add_module(module)
    try:
        engine.module_from_json(module, midi_json("Loopback 1"))
        _, queue = loop
        device = queue.device
        assert isinstance(device, midi.InputDevice)
        views = [p for p in device.subscribed if isinstance(p, midi.PortView)]
        assert len(views) == 1 and views[0].driver_id == LOOPBACK and views[0].device_id == 0 and views[0].alive
        engine.module_from_json(module, midi_json("Loopback 2"))
        assert not alive(views[0])  # it left this device
        with pytest.raises(StaleHandleError):
            _ = views[0].channel
    finally:
        engine.remove_module(module)


def test_runnable_midi_snippet() -> None:
    from pathlib import Path
    from typing import Any

    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "midi_loopback.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    out, log = scope["open_loopback"](5)
    assert out.get_device_name(5) == "Loopback 6" and log.notes == []
    out.send_message(scope["note_on"](64))
    assert log.notes == []  # delivered on the UI thread at RackPy's next step, not here
    for port in (out, log):
        port.set_device_id(-1)
