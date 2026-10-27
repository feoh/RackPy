# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-22F: the laboratory audio driver and device, called by Rack's own audio::Port methods, inside Rack."""

import gc
import weakref
from typing import Any

import _rackpy_host
import pytest

from rackpy import laboratory
from rackpy.errors import LaboratoryError, OwnershipError, StaleHandleError
from rackpy.laboratory import AudioDevice, AudioDriver
from rackpy.raw import audio

LIVE = _rackpy_host.bound_context_generation() is not None
HOOKS = _rackpy_host.TEST_HOOKS and LIVE
needs_ports = pytest.mark.skipif(not LIVE, reason="needs Rack's live context: audio ports")


class Driver(AudioDriver):
    """Overrides every audio::Driver virtual: logs the call, then answers with super()'s (renamed) answer."""

    def __init__(self, log: list[tuple[Any, ...]], name: str = "Lab driver") -> None:
        super().__init__(name)
        self.log = log
        self.ports: list[audio.Port] = []

    def get_name(self) -> str:
        self.log.append(("driver", "get_name"))
        return "Python " + super().get_name()

    def get_device_ids(self) -> list[int]:
        self.log.append(("driver", "get_device_ids"))
        return super().get_device_ids()

    def get_default_device_id(self) -> int:
        self.log.append(("driver", "get_default_device_id"))
        return super().get_default_device_id()

    def get_device_name(self, device_id: int) -> str:
        self.log.append(("driver", "get_device_name", device_id))
        return super().get_device_name(device_id)

    def get_device_num_inputs(self, device_id: int) -> int:
        self.log.append(("driver", "get_device_num_inputs", device_id))
        return super().get_device_num_inputs(device_id)

    def get_device_num_outputs(self, device_id: int) -> int:
        self.log.append(("driver", "get_device_num_outputs", device_id))
        return super().get_device_num_outputs(device_id)

    def subscribe(self, device_id: int, port: audio.Port) -> AudioDevice | None:
        self.log.append(("driver", "subscribe", device_id))
        self.ports.append(port)
        return super().subscribe(device_id, port)

    def unsubscribe(self, device_id: int, port: audio.Port) -> None:
        self.log.append(("driver", "unsubscribe", device_id))
        self.ports.append(port)
        super().unsubscribe(device_id, port)


class Device(AudioDevice):
    """Overrides every audio::Device virtual: logs the call (with the device's label), then super()."""

    def __init__(self, log: list[tuple[Any, ...]], label: str, **config: Any) -> None:
        super().__init__(**config)
        self.log = log
        self.label = label
        self.ports: list[audio.Port] = []

    def get_name(self) -> str:
        self.log.append((self.label, "get_name"))
        return super().get_name().upper()

    def get_num_inputs(self) -> int:
        self.log.append((self.label, "get_num_inputs"))
        return super().get_num_inputs()

    def get_num_outputs(self) -> int:
        self.log.append((self.label, "get_num_outputs"))
        return super().get_num_outputs()

    def get_sample_rates(self) -> list[float]:
        self.log.append((self.label, "get_sample_rates"))
        return [*super().get_sample_rates(), 96000.0]

    def get_sample_rate(self) -> float:
        self.log.append((self.label, "get_sample_rate"))
        return super().get_sample_rate()

    def set_sample_rate(self, sample_rate: float) -> None:
        self.log.append((self.label, "set_sample_rate", sample_rate))
        super().set_sample_rate(sample_rate)

    def get_block_sizes(self) -> list[int]:
        self.log.append((self.label, "get_block_sizes"))
        return super().get_block_sizes()

    def get_block_size(self) -> int:
        self.log.append((self.label, "get_block_size"))
        return super().get_block_size()

    def set_block_size(self, block_size: int) -> None:
        self.log.append((self.label, "set_block_size", block_size))
        super().set_block_size(block_size)

    def subscribe(self, port: audio.Port) -> None:
        self.log.append((self.label, "subscribe"))
        self.ports.append(port)
        super().subscribe(port)

    def unsubscribe(self, port: audio.Port) -> None:
        self.log.append((self.label, "unsubscribe"))
        self.ports.append(port)
        super().unsubscribe(port)


def lab_setup() -> tuple[list[tuple[Any, ...]], Driver, Device, Device]:
    log: list[tuple[Any, ...]] = []
    driver = Driver(log)
    config = {"num_inputs": 4, "num_outputs": 6, "sample_rates": [44100.0, 48000.0], "block_sizes": [128, 256]}
    first = Device(log, "first", name="lab device", sample_rate=48000.0, block_size=256, **config)
    second = Device(log, "second", name="other", num_inputs=1, num_outputs=0)
    assert driver.add_device(first) == 0 and driver.add_device(second) == 1
    return log, driver, first, second


def test_laboratory_audio_needs_the_laboratory() -> None:
    laboratory.disable()
    for make in (AudioDriver, AudioDevice):
        with pytest.raises(LaboratoryError, match=r"rackpy\.laboratory\.enable\(\)"):
            make()
    laboratory.enable()
    driver = AudioDriver("never listed")
    with pytest.raises(TypeError):
        audio.add_driver(77, driver)  # type: ignore[arg-type]  # Rack lists only PythonDrivers it owns
    assert 77 not in audio.get_driver_ids()
    if LIVE:
        port = audio.Port()
        laboratory.disable()
        with pytest.raises(LaboratoryError):
            driver.connect(port)


def test_audio_super_is_the_configured_answer() -> None:
    driver = AudioDriver("plain")
    device = AudioDevice("dev", num_inputs=3, num_outputs=5, sample_rates=[48000.0, 44100.0], block_sizes=[64])
    assert driver.get_device_ids() == [] and driver.get_default_device_id() == -1 and driver.get_device_name(0) == ""
    assert driver.add_device(device) == 0
    with pytest.raises(OwnershipError, match="already has"):
        driver.add_device(device)
    assert driver.get_device(0) is device and driver.get_device(1) is None
    assert (driver.get_name(), driver.get_device_ids(), driver.get_default_device_id()) == ("plain", [0], 0)
    assert driver.get_device_name(0) == "dev"
    assert (driver.get_device_num_inputs(0), driver.get_device_num_outputs(0)) == (3, 5)
    assert device.get_sample_rates() == [44100.0, 48000.0] and device.get_block_sizes() == [64]
    assert (device.get_sample_rate(), device.get_block_size()) == (48000.0, 256)
    device.set_sample_rate(44100.0)
    device.set_block_size(64)
    assert device.take_requests() == [("sample_rate", 44100.0), ("block_size", 64.0)]
    assert device.sample_rate == 44100.0 and device.block_size == 64
    driver.default_device_id = 5
    assert driver.get_default_device_id() == -1  # no such device
    driver.remove_device(device)
    with pytest.raises(OwnershipError, match="does not have"):
        driver.remove_device(device)


def test_an_audio_driver_and_its_devices_are_collected() -> None:
    class Cyclic(AudioDevice):
        driver: AudioDriver

    driver = AudioDriver()
    device = Cyclic()
    device.driver = driver  # a cycle through the driver's native reference to its device
    driver.add_device(device)
    refs: list[weakref.ref[Any]] = [weakref.ref(driver), weakref.ref(device)]
    del driver, device
    gc.collect()
    assert [r() for r in refs] == [None, None]


@needs_ports
def test_racks_audio_port_calls_a_python_driver_and_device() -> None:
    log, driver, first, second = lab_setup()
    port = audio.Port()
    driver.connect(port)  # Rack's Port::setDriverId: the default device, subscribed through the driver
    assert log == [("driver", "get_default_device_id"), ("driver", "subscribe", 0), ("first", "subscribe")]
    assert first.subscribed == [port] and port.get_device_id() == 0 and port.device is not None
    assert port.get_driver_id() == -1 and port.driver is None  # Rack does not list a laboratory driver
    assert all(p is port for p in driver.ports + first.ports)  # the port's own Python object
    log.clear()

    # Rack's audio::Port methods, each reaching the Python override synchronously.
    assert port.get_driver_name() == "Python Lab driver"
    assert port.get_device_ids() == [0, 1]
    assert port.get_device_name(1) == "OTHER"
    assert (port.get_device_num_inputs(0), port.get_device_num_outputs(0)) == (4, 6)
    assert (port.get_num_inputs(), port.get_num_outputs()) == (4, 6)
    assert port.get_sample_rates() == [44100.0, 48000.0, 96000.0]
    assert port.get_sample_rate() == 48000.0
    port.set_sample_rate(44100.0)
    assert port.get_block_sizes() == [128, 256] and port.get_block_size() == 256
    port.set_block_size(128)
    assert first.take_requests() == [("sample_rate", 44100.0), ("block_size", 128.0)]
    data = port.to_json()
    assert (data["driver"], data["deviceName"]) == (-1, "LAB DEVICE")
    assert (data["sampleRate"], data["blockSize"]) == (44100.0, 128)
    assert port.device is not None and port.device.get_name() == "LAB DEVICE"  # the device view, too
    assert log == [
        ("driver", "get_name"),
        ("driver", "get_device_ids"),
        ("driver", "get_device_name", 1),
        ("second", "get_name"),
        ("driver", "get_device_num_inputs", 0),
        ("first", "get_num_inputs"),
        ("driver", "get_device_num_outputs", 0),
        ("first", "get_num_outputs"),
        ("first", "get_num_inputs"),
        ("first", "get_num_outputs"),
        ("first", "get_sample_rates"),
        ("first", "get_sample_rate"),
        ("first", "set_sample_rate", 44100.0),
        ("first", "get_block_sizes"),
        ("first", "get_block_size"),
        ("first", "set_block_size", 128),
        ("first", "get_name"),
        ("first", "get_sample_rate"),
        ("first", "get_block_size"),
        ("first", "get_name"),
    ]
    log.clear()

    port.set_device_id(1)  # Rack's: unsubscribe from the old device, then subscribe to the new one
    port.set_device_id(-1)
    assert log == [
        ("driver", "unsubscribe", 0),
        ("first", "unsubscribe"),
        ("driver", "subscribe", 1),
        ("second", "subscribe"),
        ("driver", "unsubscribe", 1),
        ("second", "unsubscribe"),
    ]
    assert first.subscribed == [] and second.subscribed == [] and port.device is None


@needs_ports
def test_audio_override_errors_reach_the_port_methods_caller() -> None:
    stranger = AudioDevice("stranger")

    class Failing(AudioDevice):
        def get_sample_rate(self) -> float:
            raise ValueError("no rate")

        def set_block_size(self, block_size: int) -> None:
            raise ValueError("no block size")

    class Picky(AudioDriver):
        def subscribe(self, device_id: int, port: audio.Port) -> AudioDevice | None:
            if device_id == 1:
                return stranger  # not this driver's: Rack's port could outlive it
            if device_id == 2:
                raise KeyError("busy")
            return super().subscribe(device_id, port)

    driver = Picky()
    device = Failing()
    driver.add_device(device)
    port = audio.Port()
    driver.connect(port)
    with pytest.raises(ValueError, match="no rate"):
        port.get_sample_rate()
    with pytest.raises(ValueError, match="no block size"):
        port.set_block_size(64)  # with the GIL held, so the error is the caller's
    with pytest.raises(OwnershipError, match="one of this driver's devices"):
        port.set_device_id(1)
    assert port.get_device_id() == -1 and port.device is None and device.subscribed == []
    with pytest.raises(KeyError, match="busy"):
        port.set_device_id(2)
    port.set_device_id(0)
    assert device.subscribed == [port]


@needs_ports
def test_audio_teardown_runs_no_python_and_leaves_ports_pointing_nowhere() -> None:
    log, driver, first, second = lab_setup()
    port = audio.Port()
    driver.connect(port)
    retained = first.ports[0]  # the port handed to subscribe
    assert retained is port
    view = retained.device
    assert view is not None and view.get_name() == "LAB DEVICE"
    log.clear()

    driver.remove_device(first)  # unplugged: the port leaves it natively
    assert log == [] and first.subscribed == []
    assert port.device is None and port.get_device_id() == -1 and port.get_sample_rate() == 0.0
    with pytest.raises(StaleHandleError, match="no longer holds"):
        view.get_name()
    assert port.get_device_ids() == [1]
    log.clear()

    port.set_device_id(1)
    assert second.subscribed == [port]
    log.clear()
    del port, retained, view
    for holder in (driver, first, second):
        holder.ports.clear()
    gc.collect()  # the port is deleted: Rack's ~Port unsubscribes through the driver, natively
    assert log == [] and second.subscribed == []

    driver.default_device_id = 1
    port = audio.Port()
    driver.connect(port)
    assert second.subscribed == [port]
    driver.ports.clear()
    second.ports.clear()
    log.clear()
    del driver
    gc.collect()  # the driver goes: its ports leave their devices and point nowhere, natively
    assert log == [] and second.subscribed == []
    assert port.get_driver_name() == "" and port.get_device_id() == -1 and port.device is None
    assert port.get_device_ids() == []
    del second
    gc.collect()


@needs_ports
def test_inconsistent_audio_overrides_leave_no_dangling_ports() -> None:
    class Leaky(AudioDriver):
        def subscribe(self, device_id: int, port: audio.Port) -> AudioDevice | None:
            super().subscribe(device_id, port)  # the device has the port...
            return None  # ...but Rack's port gets no device

    class Sloppy(AudioDevice):
        def subscribe(self, port: audio.Port) -> None:
            pass  # Rack's port gets this device, which never lists the port

    leaky = Leaky()
    held = AudioDevice("held")
    leaky.add_device(held)
    port = audio.Port()
    leaky.connect(port)
    assert port.device is None and held.subscribed == [port]
    del port
    gc.collect()
    assert held.subscribed == []  # the released port left every laboratory device

    plain = AudioDriver()
    sloppy = Sloppy("sloppy")
    plain.add_device(sloppy)
    port = audio.Port()
    plain.connect(port)
    assert port.get_device_id() == 0 and sloppy.subscribed == []
    plain.remove_device(sloppy)
    assert port.device is None and port.get_device_id() == -1


@pytest.mark.skipif(not HOOKS, reason="needs the test hooks and Rack's live context")
def test_rack_threads_never_run_laboratory_audio_python() -> None:
    log, driver, first, _ = lab_setup()
    port = audio.Port()
    refused = _rackpy_host.dispatch_stats()["refused_rack_thread"]
    got = _rackpy_host._testing.laboratory_audio_on_rack_thread(driver, first, port)
    assert log == []  # no Python override ran
    assert _rackpy_host.dispatch_stats()["refused_rack_thread"] - refused == 24  # every virtual call was refused
    assert got == {
        "driver_name": "Lab driver",
        "device_ids": [0, 1],
        "default_device_id": 0,
        "device_name": "lab device",
        "device_num_inputs": 4,
        "device_num_outputs": 6,
        "name": "lab device",
        "num_inputs": 4,
        "num_outputs": 6,
        "sample_rates": 2,
        "sample_rate": 48000.0,
        "block_sizes": 2,
        "block_size": 256,
        "subscribed": True,
    }
    assert first.take_requests() == [("sample_rate", 48000.0), ("block_size", 256.0)]
    assert first.subscribed == []


@needs_ports
def test_a_subscribe_override_that_removes_its_device_leaves_the_port_pointing_nowhere() -> None:
    gone: list[weakref.ref[AudioDevice]] = []
    driver = AudioDriver()

    class Vanishing(AudioDevice):
        def subscribe(self, port: audio.Port) -> None:
            super().subscribe(port)
            gone.append(weakref.ref(self))
            driver.remove_device(self)  # the driver held the only other reference

    driver.add_device(Vanishing("vanishing"))
    port = audio.Port()
    driver.connect(port)  # Rack's setDeviceId: the driver's subscribe, then the device's
    gc.collect()
    assert [r() for r in gone] == [None]  # freed once the override returned
    assert port.device is None and port.get_device_id() == -1 and port.get_sample_rate() == 0.0
    assert driver.get_device_ids() == []


@needs_ports
def test_audio_driver_teardown_detaches_ports_before_its_devices_go() -> None:
    port = audio.Port()
    seen: list[tuple[list[int], str, bool]] = []

    class Watched(AudioDevice):
        def __del__(self) -> None:  # during the driver's teardown: Rack's port asks its driver
            seen.append((port.get_device_ids(), port.get_driver_name(), port.device is None))

    driver = AudioDriver("going")
    driver.add_device(Watched("watched"))
    driver.connect(port)
    assert port.get_device_id() == 0
    del driver
    gc.collect()
    assert seen == [([], "", True)]


@needs_ports
def test_an_audio_driver_an_override_drops_inside_racks_port_code_goes_after_it() -> None:
    holder: dict[str, AudioDriver] = {}

    class DropsOnUnsubscribe(AudioDevice):
        def unsubscribe(self, port: audio.Port) -> None:
            super().unsubscribe(port)
            holder.clear()  # the last reference: Rack's setDeviceId then subscribes through the driver

    class DropsOnSubscribe(AudioDevice):
        def subscribe(self, port: audio.Port) -> None:
            super().subscribe(port)
            holder.clear()  # the native AudioDriver::subscribe that called this still uses the driver

    driver = AudioDriver()
    driver.add_device(DropsOnUnsubscribe("first"))
    driver.add_device(AudioDevice("second"))
    port = audio.Port()
    driver.connect(port)
    gone = weakref.ref(driver)
    holder["driver"] = driver
    del driver
    port.set_device_id(1)
    gc.collect()
    assert gone() is None and port.device is None and port.get_device_id() == -1

    driver = AudioDriver()
    driver.add_device(DropsOnSubscribe("subscribing"))
    driver.default_device_id = -1
    port = audio.Port()
    driver.connect(port)  # no default device: nothing subscribed yet
    assert port.device is None
    gone = weakref.ref(driver)
    holder["driver"] = driver
    del driver
    port.set_device_id(0)
    gc.collect()
    assert gone() is None and port.device is None and port.get_device_id() == -1
