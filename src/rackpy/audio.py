# SPDX-License-Identifier: GPL-3.0-or-later
"""Audio, Pythonically, over :mod:`rackpy.raw.audio`. Inside Rack's live UI only.

Everything here is synchronous and runs on the calling thread. Rack's audio
thread never runs Python: a :class:`~rackpy.raw.audio.Port` subclass's
``on_start_stream``/``on_stop_stream`` arrive queued on the UI thread, and its
process callbacks run only for a device that Python drives (:class:`Stream`).
The raw layer's rules apply unchanged (see the audio reference page).
"""

from array import array
from typing import overload

from rackpy._selection import by_name, unique_names
from rackpy.raw.audio import Driver, Port, PythonDevice, PythonDriver, add_driver, get_driver, get_driver_ids

__all__ = ["Stream", "drivers", "find_driver", "open_port", "virtual_driver"]


def drivers() -> dict[str, Driver]:
    """Rack's audio drivers by name; raise LookupError on duplicate names."""
    found = (get_driver(i) for i in get_driver_ids())
    return unique_names(((d.get_name(), d) for d in found if d is not None), "audio driver")


def find_driver(driver: str | int) -> Driver:
    """A driver by name or ID. LookupError if absent or the name is ambiguous."""
    if isinstance(driver, str):
        found = (get_driver(i) for i in get_driver_ids())
        return by_name(((d.get_name(), d) for d in found if d is not None), driver, "audio driver")
    result = get_driver(driver)
    if result is None:
        raise LookupError(f"Rack has no audio driver {driver!r}")
    return result


@overload
def open_port[P: Port](driver: str | int, device: str | int | None = None, *, kind: type[P]) -> P: ...


@overload
def open_port(driver: str | int, device: str | int | None = None) -> Port: ...


def open_port(driver: str | int, device: str | int | None = None, *, kind: type[Port] = Port) -> Port:
    """A new audio port (``kind``, a Port subclass) on a driver's device, by name or ID (None: the driver's default)."""
    d = find_driver(driver)
    port = kind()
    port.set_driver_id(d.id)
    if device is None:
        return port
    if isinstance(device, str):
        names = ((d.get_device_name(i), i) for i in d.get_device_ids())
        device = by_name(names, device, f"{d.get_name()} audio device")
    port.set_device_id(device)
    if port.get_device_id() != device:
        raise LookupError(f"{d.get_name()} could not open audio device {device}")
    return port


def virtual_driver(driver_id: int, name: str, devices: tuple[str, ...] = ("Virtual 1",)) -> PythonDriver:
    """A Python audio driver with these devices (2 in, 2 out, 48 kHz, 256 frames), given to Rack until it quits."""
    driver = PythonDriver(name)
    for device in devices:
        driver.add_device(device)
    add_driver(driver_id, driver)
    return driver


class Stream:
    """Drives a PythonDevice deterministically: each ``process()`` call is one block, as an audio driver's callback.

    Rack's ports run on this thread: Core's Audio module, as master, steps Rack's engine by the block; a Python
    port's overrides run synchronously. Buffers are interleaved float32 frames.
    """

    def __init__(self, device: PythonDevice) -> None:
        self.device = device
        self.running = False

    def start(self) -> None:
        self.device.start_stream()
        self.running = True

    def stop(self) -> None:
        if self.running:
            self.device.stop_stream()
            self.running = False

    def process(self, frames: int | None = None, input: array[float] | None = None) -> array[float]:
        """One block of ``frames`` (default: the device's block size); returns the output frames."""
        n = self.device.block_size if frames is None else frames
        ins, outs = self.device.num_inputs, self.device.num_outputs
        data = input if input is not None else array("f", bytes(4 * n * ins))
        out = array("f", bytes(4 * n * outs))
        self.device.process_buffer(data, ins, out, outs, n)
        return out

    def __enter__(self) -> Stream:
        self.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self.stop()
