# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-29C: audio and MIDI drivers and devices that Python configures, in Rack's live UI: the deterministic fixture
that drives Rack's own ports (Core's Audio-2 and MIDI-CV modules) and Python's."""

import gc
from array import array
from typing import Any

import _rackpy_host
import pytest

from rackpy import audio as fa
from rackpy import midi as fm
from rackpy import raw
from rackpy.errors import OwnershipError, StaleHandleError
from rackpy.raw import audio, midi
from rackpy.raw.engine import Engine, EngineModule

AUDIO_ID, MIDI_ID = 4321, 4322
STATE: dict[str, Any] = {}


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def core(slug: str, data: dict[str, Any]) -> EngineModule:
    model = raw.plugin.get_model("Core", slug)
    assert model is not None
    module = model.create_module()
    assert isinstance(module, EngineModule)
    engine().add_module(module)
    engine().module_from_json(module, {"plugin": "Core", "model": slug, "data": data})
    return module


class Recorder(audio.Port):
    def __init__(self) -> None:
        super().__init__()
        self.log: list[Any] = []

    def on_start_stream(self) -> None:
        self.log.append("start")

    def on_stop_stream(self) -> None:
        self.log.append("stop")

    def process_input(self, input: Any, input_stride: int, frames: int) -> None:
        self.log.append(("input", list(input[:4]), input_stride, frames))

    def process_buffer(self, input: Any, input_stride: int, output: Any, output_stride: int, frames: int) -> None:
        self.log.append(("buffer", frames))
        for i in range(frames):
            output[i * output_stride] = 0.25  # the device's channel 1

    def process_output(self, output: Any, output_stride: int, frames: int) -> None:
        self.log.append(("output", output[0]))


def alive(x: Any) -> bool:
    return bool(x.alive)  # a call, so that type checkers do not narrow it between Rack calls


def test_a_python_audio_driver() -> None:
    driver = audio.PythonDriver("RackPy Test Audio")
    device = driver.add_device("Fake 1", 2, 2, [44100, 48000], 48000, [64, 256], 256)
    assert driver.registered_id == -1 and driver.get_device_ids() == [0] and driver.default_device_id == 0
    with pytest.raises(ValueError, match="reserves"):
        audio.add_driver(-1, driver)
    audio.add_driver(AUDIO_ID, driver)
    STATE["audio"] = (driver, device)
    with pytest.raises(OwnershipError, match="already has"):
        audio.add_driver(AUDIO_ID + 1, driver)
    with pytest.raises(OwnershipError, match="ID"):
        audio.add_driver(AUDIO_ID, audio.PythonDriver("again"))
    view = audio.get_driver(AUDIO_ID)
    assert view is not None and view.get_name() == "RackPy Test Audio" and view.get_device_ids() == [0]
    assert view.get_device_name(0) == "Fake 1" and view.get_device_num_inputs(0) == 2
    assert fa.find_driver("RackPy Test Audio") == view


def test_python_ports_on_a_python_device() -> None:
    _, device = STATE["audio"]
    port = fa.open_port(AUDIO_ID, "Fake 1", kind=Recorder)
    assert port.device is not None and port.device.get_name() == "Fake 1" and port in device.subscribed
    held = port.get_device()  # Rack's getters, on a port that holds a device
    assert held is not None and held.get_name() == "Fake 1" and held.get_num_inputs() == 2
    assert port.get_device_id() == device.id == 0 and port.get_driver_id() == AUDIO_ID
    assert port.get_driver_name() == "RackPy Test Audio"
    assert port.log == []  # Rack's setDeviceId called on_start_stream off Python's driving: queued for the UI step
    device.start_stream()  # on Python's driving thread: synchronous
    assert port.log == ["start"]
    frames = 64
    data = array("f", [float(i) for i in range(frames * 2)])
    out = array("f", bytes(4 * frames * 2))
    device.process_buffer(data, 2, out, 2, frames)
    assert port.log[1] == ("input", [0.0, 1.0, 2.0, 3.0], 2, frames)
    assert port.log[2] == ("buffer", frames) and port.log[3] == ("output", 0.25)
    assert out[0] == 0.25 and out[1] == 0.0 and out[2 * (frames - 1)] == 0.25  # the override's writes reach the device
    assert port.stream_stats["buffers"] == 1 and port.stream_stats["frames"] == frames
    with pytest.raises(ValueError, match="at least"):
        device.process_buffer(data, 2, out, 2, frames * 4)  # buffers too short
    with pytest.raises(ValueError, match="stride"):
        device.process_buffer(data, 1, out, 2, 8)
    device.stop_stream()  # Rack's Device::onStopStream, as driver code calls it: each port's on_stop_stream
    assert port.log[-1] == "stop"
    device.start_stream()
    assert port.log[-1] == "start"
    STATE["port"] = port


def test_core_audio_steps_rack_through_a_python_device() -> None:
    _, device = STATE["audio"]
    e = engine()
    module = core(
        "AudioInterface2",
        {"audio": {"driver": AUDIO_ID, "deviceName": "Fake 1", "sampleRate": 48000.0, "blockSize": 256}},
    )
    views = [p for p in device.subscribed if isinstance(p, audio.PortView)]
    assert len(views) == 1 and views[0].driver_id == AUDIO_ID and views[0].device_id == 0
    requests = device.take_requests()  # Rack's port asked for the saved rate and block size
    assert ("sample_rate", 48000.0) in requests and ("block_size", 256.0) in requests
    stream = fa.Stream(device)
    stream.process()  # Core's Audio claims the master module on its first block
    assert e.get_master_module() == module
    before = e.get_frame()
    for _ in range(8):
        stream.process(256)
    assert e.get_frame() - before >= 8 * 256 and e.get_block_frames() == 256  # the device's blocks step the engine
    STATE["core"] = module


def test_active_stream_module_deletion_and_hotplug() -> None:
    driver, device = STATE["audio"]
    port: Recorder = STATE["port"]
    module: EngineModule = STATE["core"]
    e = engine()
    e.remove_module(module)  # Rack unsets it as master; Python owns it, mid-stream
    del module
    STATE.pop("core")
    gc.collect()  # deleted on the main thread: its port leaves the device
    assert [p for p in device.subscribed if isinstance(p, audio.PortView)] == []
    fa.Stream(device).process(64)  # the stream goes on with Python's port alone
    assert port.log[-1][0] == "output"
    driver.remove_device(device)  # unplugged: the stream stops, Rack no longer lists it
    assert port.log[-1] == "stop" and driver.get_device_ids() == [] and device.removed and alive(device)
    with pytest.raises(StaleHandleError, match="removed"):
        fa.Stream(device).process(64)
    port.set_device_id(-1)  # the last port leaves: the device goes
    assert not alive(device)
    second = driver.add_device("Fake 2")  # plugged in: a new, stable ID
    assert driver.get_device_ids() == [1] and second.id == 1 and second.name == "Fake 2"


def test_a_python_midi_driver() -> None:
    driver = fm.virtual_driver(MIDI_ID, "RackPy Test MIDI", inputs=("Fake In",), outputs=("Fake Out",))
    STATE["midi"] = driver
    assert driver.registered_id == MIDI_ID and fm.find_driver(MIDI_ID).get_name() == "RackPy Test MIDI"
    fin, fout = driver.add_input_device("Fake In 2"), driver.add_output_device("Fake Out 2")
    view = midi.get_driver(MIDI_ID)
    assert view is not None and view.get_input_device_ids() == [0, 1] and view.get_output_device_name(1) == "Fake Out 2"

    queue: midi.InputQueue = fm.open_input(MIDI_ID, "Fake In 2")
    for note in (60, 62, 64):
        fin.push_message(fm.note_on(note))
    frames = []
    while (got := queue.try_pop(2**62)) is not None:
        frames.append(got.frame)
        assert got.get_status() == 0x9
    assert len(frames) == 3 and frames == sorted(frames) and frames[0] >= 0  # in order, timestamped by Rack

    out = fm.open_output(MIDI_ID, "Fake Out 2", channel=3)
    out.send_message(fm.note_on(70, channel=0))
    sent = fout.take_sent()
    assert len(sent) == 1 and sent[0].get_channel() == 3 and sent[0].get_note() == 70
    for _ in range(1500):
        out.send_message(fm.control_change(1, 2))
    assert len(fout.take_sent()) == 1024 and fout.dropped == 476  # the bounded queue drops the rest, and counts them

    cv = core("MIDIToCVInterface", {"midi": {"driver": MIDI_ID, "deviceName": "Fake In 2"}})
    assert any(isinstance(p, midi.PortView) for p in fin.subscribed) and queue in fin.subscribed
    engine().remove_module(cv)
    del cv
    gc.collect()

    driver.remove_input_device(fin)  # unplugged: Rack no longer lists it; the queue keeps it until it leaves
    assert view.get_input_device_ids() == [0] and fin.removed and alive(fin)
    with pytest.raises(StaleHandleError, match="removed"):
        fin.push_message(fm.note_on(1))
    queue.set_device_id(-1)
    assert not alive(fin)
    out.set_device_id(-1)
    driver.remove_output_device(fout)
    assert not alive(fout)


def test_message_helpers() -> None:
    assert fm.note_on(60, 100, channel=2).bytes == [0x92, 60, 100]
    assert fm.note_off(60).bytes == [0x80, 60, 0] and fm.control_change(7, 127, 15).bytes == [0xBF, 7, 127]
    assert fm.message(0xF8, frame=5).frame == 5
    with pytest.raises(ValueError, match="channel"):
        fm.note_on(1, channel=16)
    with pytest.raises(ValueError, match="velocity"):
        fm.note_on(1, 200)
    with pytest.raises(LookupError):
        fm.find_driver("no such driver")


def test_runnable_virtual_audio_snippet() -> None:
    from pathlib import Path

    path = Path(__file__).resolve().parents[3] / "docs" / "snippets" / "virtual_audio.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    assert scope["render"](4) == [0.0, 0.25, 0.5, 0.75]


def test_audio_same_port_native_stream_reentry_and_nonvirtual_super() -> None:
    driver = audio.PythonDriver("Reentrant audio")
    device = driver.add_device("Offline", 0, 0)
    audio.add_driver(4331, driver)
    calls = 0
    recursive = False

    class Reentrant(audio.Port):
        def on_start_stream(self) -> None:
            nonlocal calls
            calls += 1
            super().on_start_stream()  # no-op, not virtual redispatch
            if recursive or calls == 1:
                device.start_stream()

    port = Reentrant()
    port.set_driver_id(4331)
    port.set_device_id(device.id)
    try:
        device.start_stream()
        assert calls == 2
        calls, recursive = 0, True
        with pytest.raises(RecursionError, match="overrides nested more than 64"):
            device.start_stream()
        assert calls == 64
        recursive = False
        device.start_stream()
        assert calls == 65
    finally:
        port.set_device_id(-1)


def test_rack_deletes_python_drivers_as_it_quits() -> None:
    """Rack's midi::destroy() and audio::destroy() delete every driver (its virtual ~Driver) before the plugin's
    destroy(), so a destroy callback finds the Python drivers' handles stale. The outer test reads Rack's log."""
    audio_driver, _ = STATE["audio"]
    midi_driver = STATE["midi"]
    spare = audio_driver.get_device(1)  # "Fake 2", plugged in by test_active_stream_module_deletion_and_hotplug
    assert spare is not None and spare.alive
    assert audio_driver.name == "RackPy Test Audio" and midi_driver.name == "RackPy Test MIDI"

    @raw.plugin.on_destroy
    def deleted() -> None:
        for kind, driver in (("audio", audio_driver), ("midi", midi_driver)):
            try:
                name = driver.name
            except StaleHandleError as e:
                _rackpy_host.log_info(f"rp31e {kind} driver deleted: {e}")
            else:
                _rackpy_host.log_info(f"rp31e {kind} driver still alive: {name}")
        _rackpy_host.log_info(f"rp31e audio device alive: {spare.alive}")  # the driver's devices go with it
