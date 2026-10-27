# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22F: the laboratory MIDI driver, devices and inputs, called by Rack's own midi:: code, inside Rack."""

import gc
import weakref
from typing import Any

import _rackpy_host
import pytest

from rackpy import laboratory
from rackpy.errors import LaboratoryError, OwnershipError, StaleHandleError
from rackpy.laboratory import MidiDriver, MidiInput, MidiInputDevice, MidiInputQueue, MidiOutputDevice
from rackpy.raw import app, midi
from rackpy.raw.math import Vec

LIVE = _rackpy_host.bound_context_generation() is not None
HOOKS = _rackpy_host.TEST_HOOKS and LIVE
LOOPBACK = -12
needs_ports = pytest.mark.skipif(not LIVE, reason="needs Rack's live context: MIDI ports")

Log = list[tuple[Any, ...]]


class Driver(MidiDriver):
    """Overrides every midi::Driver virtual: logs the call, then answers with super()'s answer."""

    def __init__(self, log: Log, name: str = "Lab MIDI") -> None:
        super().__init__(name)
        self.log = log
        self.ports: list[midi.Port] = []

    def get_name(self) -> str:
        self.log.append(("driver", "get_name"))
        return "Python " + super().get_name()

    def get_input_device_ids(self) -> list[int]:
        self.log.append(("driver", "get_input_device_ids"))
        return super().get_input_device_ids()

    def get_default_input_device_id(self) -> int:
        self.log.append(("driver", "get_default_input_device_id"))
        return super().get_default_input_device_id()

    def get_input_device_name(self, device_id: int) -> str:
        self.log.append(("driver", "get_input_device_name", device_id))
        return super().get_input_device_name(device_id)

    def subscribe_input(self, device_id: int, input: midi.Input) -> MidiInputDevice | None:
        self.log.append(("driver", "subscribe_input", device_id))
        self.ports.append(input)
        return super().subscribe_input(device_id, input)

    def unsubscribe_input(self, device_id: int, input: midi.Input) -> None:
        self.log.append(("driver", "unsubscribe_input", device_id))
        self.ports.append(input)
        super().unsubscribe_input(device_id, input)

    def get_output_device_ids(self) -> list[int]:
        self.log.append(("driver", "get_output_device_ids"))
        return super().get_output_device_ids()

    def get_default_output_device_id(self) -> int:
        self.log.append(("driver", "get_default_output_device_id"))
        return super().get_default_output_device_id()

    def get_output_device_name(self, device_id: int) -> str:
        self.log.append(("driver", "get_output_device_name", device_id))
        return super().get_output_device_name(device_id)

    def subscribe_output(self, device_id: int, output: midi.Output) -> MidiOutputDevice | None:
        self.log.append(("driver", "subscribe_output", device_id))
        self.ports.append(output)
        return super().subscribe_output(device_id, output)

    def unsubscribe_output(self, device_id: int, output: midi.Output) -> None:
        self.log.append(("driver", "unsubscribe_output", device_id))
        self.ports.append(output)
        super().unsubscribe_output(device_id, output)


class InDevice(MidiInputDevice):
    def __init__(self, log: Log, label: str, name: str) -> None:
        super().__init__(name)
        self.log = log
        self.label = label

    def get_name(self) -> str:
        self.log.append((self.label, "get_name"))
        return super().get_name().upper()


class OutDevice(MidiOutputDevice):
    def __init__(self, log: Log, label: str, name: str) -> None:
        super().__init__(name)
        self.log = log
        self.label = label
        self.sent: list[list[int]] = []
        self.messages: list[midi.Message] = []

    def get_name(self) -> str:
        self.log.append((self.label, "get_name"))
        return super().get_name().upper()

    def send_message(self, message: midi.Message) -> None:
        self.sent.append(message.bytes)
        self.messages.append(message)
        super().send_message(message)


def lab_setup() -> tuple[Log, Driver, InDevice, InDevice, OutDevice]:
    log: Log = []
    driver = Driver(log)
    first, second = InDevice(log, "in0", "first input"), InDevice(log, "in1", "second")
    out = OutDevice(log, "out0", "an output")
    assert (driver.add_input_device(first), driver.add_input_device(second), driver.add_output_device(out)) == (0, 1, 0)
    return log, driver, first, second, out


def message(*data: int, frame: int = -1) -> midi.Message:
    m = midi.Message()
    m.bytes = list(data)
    m.frame = frame
    return m


def test_laboratory_midi_needs_the_laboratory() -> None:
    laboratory.disable()
    for make in (MidiDriver, MidiInputDevice, MidiOutputDevice):
        with pytest.raises(LaboratoryError, match=r"rackpy\.laboratory\.enable\(\)"):
            make()
    if LIVE:
        for cls in (MidiInput, MidiInputQueue):
            with pytest.raises(LaboratoryError, match=r"rackpy\.laboratory\.enable\(\)"):
                cls()
        with pytest.raises(LaboratoryError):
            _rackpy_host.laboratory.mark_midi_input(midi.Input())
    laboratory.enable()
    driver = MidiDriver("never listed")
    with pytest.raises(TypeError):
        midi.add_driver(77, driver)  # type: ignore[arg-type]  # Rack lists only PythonDrivers it owns
    assert 77 not in midi.get_driver_ids()
    with pytest.raises(TypeError, match="right direction"):
        driver.add_input_device(MidiOutputDevice())  # type: ignore[arg-type]
    if LIVE:
        port = midi.Input()
        assert not _rackpy_host.laboratory.is_laboratory_midi_input(port)
        laboratory.disable()
        with pytest.raises(LaboratoryError):
            driver.connect(port)


def test_midi_super_is_the_configured_answer() -> None:
    _, driver, first, second, out = lab_setup()
    assert MidiDriver.get_name(driver) == "Lab MIDI"  # Rack's virtual, natively: the configured name
    assert MidiDriver.get_input_device_ids(driver) == [0, 1]
    assert MidiDriver.get_default_input_device_id(driver) == 0
    assert MidiDriver.get_input_device_name(driver, 1) == "SECOND"  # the device's own get_name is virtual
    assert MidiDriver.get_input_device_name(driver, 7) == ""
    assert MidiDriver.get_output_device_ids(driver) == [0] and MidiDriver.get_default_output_device_id(driver) == 0
    assert MidiDriver.get_output_device_name(driver, 0) == "AN OUTPUT"
    assert MidiInputDevice.get_name(first) == "first input" and MidiOutputDevice.get_name(out) == "an output"
    assert driver.get_input_device(1) is second and driver.get_output_device(0) is out
    driver.default_input_device_id = 9
    assert MidiDriver.get_default_input_device_id(driver) == -1
    driver.remove_output_device(out)
    with pytest.raises(OwnershipError, match="does not have"):
        driver.remove_output_device(out)


def test_a_midi_driver_and_its_devices_are_collected() -> None:
    class Cyclic(MidiInputDevice):
        driver: MidiDriver

    driver = MidiDriver()
    device = Cyclic()
    device.driver = driver
    driver.add_input_device(device)
    refs: list[weakref.ref[Any]] = [weakref.ref(driver), weakref.ref(device)]
    del driver, device
    gc.collect()
    assert [r() for r in refs] == [None, None]


@needs_ports
def test_racks_midi_ports_and_widgets_call_a_python_driver_and_devices() -> None:
    log, driver, first, second, out_device = lab_setup()
    port = midi.Input()
    driver.connect(port)  # Rack's Port::setDriverId: the default device, through the port's virtuals
    assert log == [("driver", "get_default_input_device_id"), ("driver", "subscribe_input", 0)]
    assert first.subscribed == [port] and port.get_device_id() == 0 and port.input_device is not None
    assert port.get_driver_id() == -1 and port.driver is None  # Rack does not list a laboratory driver
    assert all(p is port for p in driver.ports)
    log.clear()

    assert port.get_device_ids() == [0, 1]
    assert port.get_default_device_id() == 0
    assert port.get_device_name(1) == "SECOND"
    assert port.to_json() == {"driver": -1, "deviceName": "FIRST INPUT", "channel": -1}
    assert port.input_device.get_name() == "FIRST INPUT"
    names = []
    for cls in (app.MidiDriverChoice, app.MidiDeviceChoice):
        choice = cls()
        choice.box.size = Vec(100, 20)
        choice.port = port
        choice.step()  # Rack's widgets: the driver's and the device's names
        names.append(choice.text)
        choice.close()
    assert names == ["Python Lab MIDI", "FIRST INPUT"]
    port.set_device_id(1)
    port.set_device_id(-1)
    assert log == [
        ("driver", "get_input_device_ids"),
        ("driver", "get_default_input_device_id"),
        ("driver", "get_input_device_name", 1),
        ("in1", "get_name"),
        ("in0", "get_name"),
        ("in0", "get_name"),
        ("driver", "get_name"),
        ("in0", "get_name"),
        ("driver", "unsubscribe_input", 0),
        ("driver", "subscribe_input", 1),
        ("driver", "unsubscribe_input", 1),
    ]
    assert first.subscribed == [] and second.subscribed == []
    log.clear()

    out = midi.Output()
    driver.connect(out)
    out.channel = 3
    assert out.get_device_ids() == [0] and out.get_default_device_id() == 0
    assert out.get_device_name(0) == "AN OUTPUT"
    assert out.to_json() == {"driver": -1, "deviceName": "AN OUTPUT", "channel": 3}
    out.send_message(message(0x90, 60, 100))  # Rack's Output::sendMessage applies the channel
    assert out_device.sent == [[0x93, 60, 100]]
    assert out.output_device is not None
    raw_note = message(0x90, 61, 1)
    out.output_device.send_message(raw_note)  # Rack's OutputDevice::sendMessage: the message as it is
    assert out_device.sent[-1] == [0x90, 61, 1] and out_device.messages[-1] is not raw_note  # an owned copy
    out.set_device_id(-1)
    assert log == [
        ("driver", "get_default_output_device_id"),
        ("driver", "subscribe_output", 0),
        ("driver", "get_output_device_ids"),
        ("driver", "get_default_output_device_id"),
        ("driver", "get_output_device_name", 0),
        ("out0", "get_name"),
        ("out0", "get_name"),
        ("driver", "unsubscribe_output", 0),
    ]


@needs_ports
def test_a_laboratory_input_receives_messages_synchronously() -> None:
    received: list[midi.Message] = []

    class Listener(MidiInput):
        def on_message(self, message: midi.Message) -> None:
            received.append(message)
            super().on_message(message)  # Rack's: nothing

    driver = MidiDriver()
    device = MidiInputDevice("keys")
    driver.add_input_device(device)
    listener = Listener()
    assert _rackpy_host.laboratory.is_laboratory_midi_input(listener)
    driver.connect(listener)
    note = message(0x90, 60, 100, frame=7)
    device.push_message(note)  # Rack's InputDevice::onMessage, as a driver calls it
    assert [(m.bytes, m.frame) for m in received] == [([0x90, 60, 100], 7)]
    note.bytes = [0x80, 60, 0]
    assert received[0].bytes == [0x90, 60, 100]  # an owned copy
    device.push_message(message(0xB0, 7, 1))  # unset frame: Rack's engine timing sets it
    assert received[-1].frame >= 0
    listener.channel = 2  # Rack's channel filter
    device.push_message(message(0x90, 1, 1))
    device.push_message(message(0x92, 2, 1))
    assert [m.bytes for m in received[2:]] == [[0x92, 2, 1]]
    assert listener.dropped == 0

    # Rack's own Loopback driver, when Python sends: synchronous too.
    loop = Listener()
    loop.set_driver_id(LOOPBACK)
    loop.set_device_id(5)
    sender = midi.Output()
    sender.set_driver_id(LOOPBACK)
    sender.set_device_id(5)
    sender.send_message(message(0x90, 64, 1))
    assert received[-1].get_note() == 64 and received[-1].get_channel() == 0

    # An ordinary Python input, and one that skips the laboratory constructor, get theirs queued.
    class Ordinary(midi.Input):
        def on_message(self, message: midi.Message) -> None:
            received.append(message)

    class Unmarked(Listener):
        def __init__(self) -> None:
            midi.Input.__init__(self)

    for cls in (Ordinary, Unmarked):
        port = cls()
        assert not _rackpy_host.laboratory.is_laboratory_midi_input(port)
        driver.connect(port)
        before = len(received)
        device.push_message(message(0x90, 1, 1))
        assert len(received) == before  # delivered at the next UI step, not now
        port.set_device_id(-1)


@needs_ports
def test_a_laboratory_input_queue_overrides_racks_queue() -> None:
    seen: list[list[int]] = []
    kept: list[midi.Message] = []

    class Filter(MidiInputQueue):
        def on_message(self, message: midi.Message) -> None:
            seen.append(message.bytes)
            kept.append(message)
            if message.get_note() != 0:
                super().on_message(message)  # Rack's InputQueue: queued for try_pop

    driver = MidiDriver()
    device = MidiInputDevice()
    driver.add_input_device(device)
    queue = Filter()
    assert isinstance(queue, midi.Input) and midi.InputQueue not in type(queue).__mro__
    driver.connect(queue)
    assert device.subscribed == [queue]
    first = message(0x90, 0, 1, frame=1)
    device.push_message(first)
    device.push_message(message(0x90, 5, 1, frame=2))
    assert seen == [[0x90, 0, 1], [0x90, 5, 1]]
    assert kept[0] is not first and kept[0] == first  # an owned copy of Rack's message
    assert queue.size() == 1
    got = queue.try_pop(10)
    assert got is not None and got.get_note() == 5 and queue.try_pop(10) is None
    MidiInputQueue.on_message(queue, message(0x90, 9, 9, frame=3))  # the base: Rack's own
    assert queue.size() == 1 and len(seen) == 2


@needs_ports
def test_midi_override_errors_reach_the_caller() -> None:
    stranger = MidiInputDevice("stranger")

    class Raising(MidiInput):
        def on_message(self, message: midi.Message) -> None:
            raise ValueError("bad note")

    class Picky(MidiDriver):
        def subscribe_input(self, device_id: int, input: midi.Input) -> MidiInputDevice | None:
            if device_id == 1:
                return stranger  # not this driver's: Rack's port could outlive it
            return super().subscribe_input(device_id, input)

        def get_output_device_ids(self) -> list[int]:
            raise KeyError("no outputs")

    driver = Picky()
    device = MidiInputDevice()
    driver.add_input_device(device)
    driver.add_input_device(MidiInputDevice())
    port = Raising()
    driver.connect(port)
    with pytest.raises(ValueError, match="bad note"):
        device.push_message(message(0x90, 1, 1))
    with pytest.raises(OwnershipError, match="one of this driver's devices"):
        port.set_device_id(1)
    assert port.get_device_id() == -1 and port.device is None and device.subscribed == []
    out = midi.Output()
    driver.connect(out)
    with pytest.raises(KeyError, match="no outputs"):
        out.get_device_ids()


@needs_ports
def test_midi_teardown_runs_no_python_and_leaves_ports_pointing_nowhere() -> None:
    log, driver, first, second, out_device = lab_setup()
    port = midi.Input()
    driver.connect(port)
    retained = driver.ports[0]  # the port handed to subscribe_input
    assert retained is port
    view = retained.input_device
    assert view is not None and view.get_name() == "FIRST INPUT"
    log.clear()

    driver.remove_input_device(first)  # unplugged: the port leaves it natively
    assert log == [] and first.subscribed == []
    assert port.device is None and port.get_device_id() == -1
    with pytest.raises(StaleHandleError, match="no longer holds"):
        view.get_name()

    port.set_device_id(1)
    assert second.subscribed == [port]
    log.clear()
    del port, retained, view
    driver.ports.clear()
    gc.collect()  # Rack's ~Input unsubscribes through the driver: natively, no Python
    assert log == [] and second.subscribed == []

    driver.default_input_device_id = 1
    port = midi.Input()
    driver.connect(port)
    out = midi.Output()
    driver.connect(out)
    assert second.subscribed == [port] and out_device.connected == 1
    driver.ports.clear()
    log.clear()
    del driver
    gc.collect()  # the driver goes: its ports leave their devices and point nowhere, natively
    assert log == [] and second.subscribed == [] and out_device.connected == 0
    for p in (port, out):
        assert p.get_driver() is None and p.get_device_id() == -1 and p.device is None
        assert p.get_device_ids() == []
    del out_device
    gc.collect()
    out.send_message(message(0x90, 1, 1))  # no device: Rack sends nothing


@needs_ports
def test_inconsistent_midi_overrides_leave_no_dangling_ports() -> None:
    class Leaky(MidiDriver):
        def subscribe_input(self, device_id: int, input: midi.Input) -> MidiInputDevice | None:
            super().subscribe_input(device_id, input)  # the device has the port...
            return None  # ...but Rack's port gets no device

    class Bare(MidiDriver):
        def subscribe_input(self, device_id: int, input: midi.Input) -> MidiInputDevice | None:
            return self.get_input_device(device_id)  # Rack's port gets a device that never lists it

    leaky = Leaky()
    held = MidiInputDevice("held")
    leaky.add_input_device(held)
    port = midi.Input()
    leaky.connect(port)
    assert port.device is None and held.subscribed == [port]
    del port
    gc.collect()
    assert held.subscribed == []  # the released port left every laboratory device
    held.push_message(message(0x90, 1, 1))  # no subscriber left to reach

    bare = Bare()
    unlisted = MidiInputDevice("unlisted")
    bare.add_input_device(unlisted)
    port = midi.Input()
    bare.connect(port)
    assert port.get_device_id() == 0 and unlisted.subscribed == []
    bare.remove_input_device(unlisted)
    assert port.device is None and port.get_device_id() == -1


@pytest.mark.skipif(not HOOKS, reason="needs the test hooks and Rack's live context")
def test_rack_threads_never_run_laboratory_midi_python() -> None:
    log, driver, first, _, out_device = lab_setup()
    port, out = midi.Input(), midi.Output()
    refused = _rackpy_host.dispatch_stats()["refused_rack_thread"]
    got = _rackpy_host._testing.laboratory_midi_on_rack_thread(driver, first, out_device, port, out)
    assert log == []  # no Python override ran
    assert _rackpy_host.dispatch_stats()["refused_rack_thread"] - refused == 15  # every virtual call was refused
    assert got == {
        "driver_name": "Lab MIDI",
        "input_device_ids": [0, 1],
        "default_input_device_id": 0,
        "input_device_name": "first input",
        "input_subscribed": True,
        "output_device_ids": [0],
        "default_output_device_id": 0,
        "output_device_name": "an output",
        "output_subscribed": True,
        "input_name": "first input",
        "output_name": "an output",
    }
    assert first.subscribed == [] and out_device.connected == 0

    calls: list[list[int]] = []

    class Listener(MidiInput):
        def on_message(self, message: midi.Message) -> None:
            calls.append(message.bytes)

    class Queue(MidiInputQueue):
        def on_message(self, message: midi.Message) -> None:
            calls.append(message.bytes)

    listener, queue = Listener(), Queue()
    refused = _rackpy_host.dispatch_stats()["refused_rack_thread"]
    _rackpy_host._testing.midi_input_on_rack_thread(listener, message(0x90, 1, 1))
    _rackpy_host._testing.midi_input_on_rack_thread(queue, message(0x90, 2, 1, frame=0))
    assert calls == [] and _rackpy_host.dispatch_stats()["refused_rack_thread"] - refused == 2
    assert listener.dropped == 0  # queued for the UI thread, as for any Python input
    got_message = queue.try_pop(10)  # Rack's InputQueue took it
    assert got_message is not None and got_message.get_note() == 2
