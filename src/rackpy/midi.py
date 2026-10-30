# SPDX-License-Identifier: GPL-3.0-or-later
"""MIDI, Pythonically, over :mod:`rackpy.raw.midi`. Inside Rack's live UI only.

Everything here is synchronous and runs on Rack's main thread. A message
arrives at an :class:`~rackpy.raw.midi.Input` subclass's ``on_message`` on the
UI thread, queued (never on the MIDI driver's thread); an ``InputQueue`` keeps
messages until you pop them. The raw layer's rules apply unchanged (see the
MIDI reference page); nothing here has native semantics of its own.
"""

from typing import overload

from rackpy._selection import by_name, unique_names
from rackpy.raw.midi import (
    Driver,
    Input,
    InputQueue,
    Message,
    Output,
    PythonDriver,
    add_driver,
    get_driver,
    get_driver_ids,
)

__all__ = [
    "LOOPBACK",
    "Message",
    "control_change",
    "drivers",
    "find_driver",
    "message",
    "note_off",
    "note_on",
    "open_input",
    "open_output",
    "virtual_driver",
]

LOOPBACK = -12
"""Rack's Loopback driver: what goes out on device N comes in on device N."""


def message(*data: int, frame: int = -1) -> Message:
    """A message with these bytes (0-255) and engine frame (-1: now)."""
    msg = Message()
    msg.bytes = list(data)
    msg.frame = frame
    return msg


def note_on(note: int, velocity: int = 100, channel: int = 0, frame: int = -1) -> Message:
    """Note on (status 9): note and velocity 0-127, channel 0-15."""
    _check(note=note, velocity=velocity, channel=channel)
    return message(0x90 | channel, note, velocity, frame=frame)


def note_off(note: int, velocity: int = 0, channel: int = 0, frame: int = -1) -> Message:
    """Note off (status 8)."""
    _check(note=note, velocity=velocity, channel=channel)
    return message(0x80 | channel, note, velocity, frame=frame)


def control_change(controller: int, value: int, channel: int = 0, frame: int = -1) -> Message:
    """Control change (status 0xb)."""
    _check(controller=controller, value=value, channel=channel)
    return message(0xB0 | channel, controller, value, frame=frame)


def _check(channel: int, **data: int) -> None:
    if not 0 <= channel <= 15:
        raise ValueError(f"channel {channel} is not 0 to 15")
    for name, value in data.items():
        if not 0 <= value <= 127:
            raise ValueError(f"{name} {value} is not 0 to 127")


def drivers() -> dict[str, Driver]:
    """Rack's MIDI drivers by name; raise LookupError on duplicate names."""
    found = (get_driver(i) for i in get_driver_ids())
    return unique_names(((d.get_name(), d) for d in found if d is not None), "MIDI driver")


def find_driver(driver: str | int) -> Driver:
    """A driver by name or ID. LookupError if absent or the name is ambiguous."""
    if isinstance(driver, str):
        found = (get_driver(i) for i in get_driver_ids())
        return by_name(((d.get_name(), d) for d in found if d is not None), driver, "MIDI driver")
    result = get_driver(driver)
    if result is None:
        raise LookupError(f"Rack has no MIDI driver {driver!r}")
    return result


def _select[P: Input | Output](port: P, driver: str | int, device: str | int | None, inputs: bool) -> P:
    d = find_driver(driver)
    port.set_driver_id(d.id)
    if device is None:
        return port  # Rack's default device
    if isinstance(device, str):
        ids = d.get_input_device_ids() if inputs else d.get_output_device_ids()
        names = ((d.get_input_device_name(i) if inputs else d.get_output_device_name(i), i) for i in ids)
        device = by_name(names, device, f"{d.get_name()} MIDI {'input' if inputs else 'output'} device")
    port.set_device_id(device)
    if port.get_device_id() != device:
        raise LookupError(f"{d.get_name()} could not open MIDI device {device}")
    return port


@overload
def open_input[I: Input](
    driver: str | int, device: str | int | None = None, *, channel: int = -1, kind: type[I]
) -> I: ...


@overload
def open_input(driver: str | int, device: str | int | None = None, *, channel: int = -1) -> InputQueue: ...


def open_input(
    driver: str | int,
    device: str | int | None = None,
    *,
    channel: int = -1,
    kind: type[Input] = InputQueue,
) -> Input:
    """A new input port (an InputQueue unless ``kind`` is an Input subclass) on a driver's device, by name or ID."""
    port = _select(kind(), driver, device, True)
    port.channel = channel
    return port


def open_output(driver: str | int, device: str | int | None = None, *, channel: int = 0) -> Output:
    """A new output port on a driver's device; ``channel`` is set on every message (-1: keep the message's)."""
    port = _select(Output(), driver, device, False)
    port.channel = channel
    return port


def virtual_driver(
    driver_id: int, name: str, inputs: tuple[str, ...] = (), outputs: tuple[str, ...] = ()
) -> PythonDriver:
    """A Python MIDI driver with these input and output devices, given to Rack (which keeps it until it quits)."""
    driver = PythonDriver(name)
    for device in inputs:
        driver.add_input_device(device)
    for device in outputs:
        driver.add_output_device(device)
    add_driver(driver_id, driver)
    return driver
