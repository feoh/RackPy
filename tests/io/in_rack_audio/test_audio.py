# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29B: rack::audio in Rack's live UI. On Linux the tests isolate Rack from this machine's sound servers and
devices (tests/support.isolated_audio_env): Rack's RtAudio drivers load and list no devices. macOS and Windows
offer no such isolation, so there the drivers may list the machine's devices, and a port is put in the no-device
state explicitly (RP-42)."""

import sys

import pytest

from rackpy.raw import audio

KEPT: list[audio.Port] = []  # alive when Rack quits: RackPy gives its device back while the drivers exist


ISOLATED = sys.platform == "linux"
# Rack's RtAudio drivers on each platform (some of their names; Windows also has ASIO).
DRIVERS = {"linux": {"ALSA", "PulseAudio", "JACK"}, "darwin": set(), "win32": {"WASAPI", "DirectSound"}}[sys.platform]


def test_racks_drivers_with_no_devices() -> None:
    ids = audio.get_driver_ids()
    names = {}
    for driver_id in ids:
        driver = audio.get_driver(driver_id)
        assert driver is not None and driver.id == driver_id and driver == audio.get_driver(driver_id)
        names[driver.get_name()] = driver_id
        devices = driver.get_device_ids()
        if ISOLATED:
            assert devices == [] and driver.get_default_device_id() == -1  # isolated: no devices
        assert driver.get_default_device_id() in [*devices, -1]
        for device in devices:  # the machine's own, where Rack cannot be isolated from them
            assert isinstance(driver.get_device_name(device), str)
            assert driver.get_device_num_inputs(device) >= 0 and driver.get_device_num_outputs(device) >= 0
        missing = max(devices, default=0) + 1000  # no such device: Rack's empty answers
        assert driver.get_device_name(missing) == "" and driver.get_device_num_inputs(missing) == 0
        assert driver.get_device_num_outputs(missing) == 0
    assert set(names) >= DRIVERS and len(names) == len(ids) and audio.get_driver(-123) is None
    if sys.platform == "darwin":
        assert any("core" in name.lower() for name in names), names  # Core Audio


def test_a_port_without_a_device() -> None:
    port = audio.Port()  # Rack's: its first driver and that driver's default device (none on Linux here)
    KEPT.append(port)
    first = audio.get_driver_ids()[0]
    if not ISOLATED:
        port.set_device_id(-1)  # the machine's default device may have opened: give it back
    assert port.get_driver_id() == first == port.driver_id and port.get_device_id() == -1 == port.device_id
    driver = port.get_driver()
    assert driver is not None and port.get_driver_name() == driver.get_name() and port.device is None
    # Rack's Port::getDeviceIds is its driver's list (audio.cpp:141-145): none when isolated, else the machine's.
    assert port.get_device() is None and port.get_device_ids() == driver.get_device_ids()
    if ISOLATED:
        assert port.get_device_ids() == []
    assert (port.get_sample_rates(), port.get_sample_rate(), port.get_block_sizes(), port.get_block_size()) == (
        [],
        0,
        [],
        0,
    )
    port.set_sample_rate(48000)  # Rack does nothing without a device
    port.set_block_size(256)
    assert port.get_num_inputs() == 0 and port.get_num_outputs() == 0
    if ISOLATED:
        port.set_device_id(3)  # the driver cannot open it: Rack logs it and the port keeps no device
        assert port.get_device_id() == -1 and port.device is None
    port.set_driver_id(123456)  # unknown: Rack falls back to its first driver
    assert port.get_driver_id() == first
    names = {audio.get_driver(i).get_name(): i for i in audio.get_driver_ids()}  # type: ignore[union-attr]
    other = names["JACK"] if ISOLATED else audio.get_driver_ids()[-1]
    port.set_driver_id(other)
    if not ISOLATED:
        port.set_device_id(-1)
    assert port.get_driver_id() == other and port.get_driver_name() == audio.get_driver(other).get_name()  # type: ignore[union-attr]
    assert port.stream_stats == {
        "buffers": 0,
        "frames": 0,
        "last_frames": 0,
        "input_calls": 0,
        "output_calls": 0,
        "dropped_events": 0,
    }


def test_offsets_and_json() -> None:
    port = audio.Port()
    port.input_offset, port.output_offset, port.max_inputs, port.max_outputs = 2, 4, 6, 10
    assert (port.input_offset, port.output_offset, port.max_inputs, port.max_outputs) == (2, 4, 6, 10)
    data = port.to_json()
    assert data["driver"] == port.get_driver_id() and data["inputOffset"] == 2 and data["outputOffset"] == 4
    assert "deviceName" not in data  # no device
    other = audio.Port()
    other.from_json(data)
    assert (other.input_offset, other.output_offset, other.get_driver_id()) == (2, 4, port.get_driver_id())
    port.reset()  # Rack's: its first driver again
    assert port.get_driver_id() == audio.get_driver_ids()[0]


def test_callbacks_python_may_override() -> None:
    port = audio.Port()
    # Rack's bases do nothing; Rack's audio thread never calls Python (stream_stats counts its calls).
    port.process_buffer(None, 0, None, 0, 0)
    port.process_input(None, 0, 0)
    port.process_output(None, 0, 0)
    port.on_start_stream()
    port.on_stop_stream()


def test_subscribing_to_a_missing_device() -> None:
    port = audio.Port()
    driver = port.get_driver()
    assert driver is not None
    with pytest.raises(Exception, match="device"):
        driver.subscribe(0, port)  # Rack's RtAudio driver raises Rack's Exception for a device it cannot query
    assert port.device is None
