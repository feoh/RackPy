# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29B: rack::audio in Rack's live UI. Tests isolate Rack from this machine's sound servers and devices
(tests/support.isolated_audio_env): Rack's RtAudio drivers load and list no devices."""

import pytest

from rackpy.raw import audio

KEPT: list[audio.Port] = []  # alive when Rack quits: RackPy gives its device back while the drivers exist


def test_racks_drivers_with_no_devices() -> None:
    ids = audio.get_driver_ids()
    names = {}
    for driver_id in ids:
        driver = audio.get_driver(driver_id)
        assert driver is not None and driver.id == driver_id and driver == audio.get_driver(driver_id)
        names[driver.get_name()] = driver_id
        assert driver.get_device_ids() == [] and driver.get_default_device_id() == -1  # isolated: no devices
        assert driver.get_device_name(0) == "" and driver.get_device_num_inputs(0) == 0
        assert driver.get_device_num_outputs(0) == 0
    assert {"ALSA", "PulseAudio", "JACK"} <= set(names) and audio.get_driver(-123) is None


def test_a_port_without_a_device() -> None:
    port = audio.Port()  # Rack's: its first driver and that driver's default device (none here)
    KEPT.append(port)
    first = audio.get_driver_ids()[0]
    assert port.get_driver_id() == first == port.driver_id and port.get_device_id() == -1 == port.device_id
    driver = port.get_driver()
    assert driver is not None and port.get_driver_name() == driver.get_name() and port.device is None
    assert port.get_device() is None and port.get_device_ids() == []
    assert (port.get_sample_rates(), port.get_sample_rate(), port.get_block_sizes(), port.get_block_size()) == (
        [],
        0,
        [],
        0,
    )
    port.set_sample_rate(48000)  # Rack does nothing without a device
    port.set_block_size(256)
    assert port.get_num_inputs() == 0 and port.get_num_outputs() == 0
    port.set_device_id(3)  # the driver cannot open it: Rack logs it and the port keeps no device
    assert port.get_device_id() == -1 and port.device is None
    port.set_driver_id(123456)  # unknown: Rack falls back to its first driver
    assert port.get_driver_id() == first
    jack = next(i for i in audio.get_driver_ids() if audio.get_driver(i).get_name() == "JACK")  # type: ignore[union-attr]
    port.set_driver_id(jack)
    assert port.get_driver_name() == "JACK"
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
