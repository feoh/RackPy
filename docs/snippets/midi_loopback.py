# SPDX-License-Identifier: GPL-3.0-or-later
# rackpy-example: runs-in=rack
from rackpy.raw import midi

LOOPBACK = -12  # Rack's Loopback MIDI driver: what goes out on device N comes in on device N


class NoteLog(midi.Input):
    """Collects the notes that arrive. RackPy calls on_message on the UI thread, not the MIDI thread."""

    def __init__(self) -> None:
        super().__init__()
        self.notes: list[int] = []

    def on_message(self, message: midi.Message) -> None:
        if message.get_status() == 0x9 and message.get_value() > 0:  # note on
            self.notes.append(message.get_note())


def open_loopback(device_id: int) -> tuple[midi.Output, NoteLog]:
    """Call from Rack's UI thread: an output and a NoteLog on one loopback device."""
    out, log = midi.Output(), NoteLog()
    for port in (out, log):
        port.set_driver_id(LOOPBACK)
        port.set_device_id(device_id)
    return out, log


def note_on(note: int, velocity: int = 100) -> midi.Message:
    message = midi.Message()
    message.set_status(0x9)
    message.set_note(note)
    message.set_value(velocity)
    return message
