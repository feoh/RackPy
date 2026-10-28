# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-31J: a RackPy module's audio port, in Rack's live UI (test hooks).

One Python-driven device with four channels each way feeds Core's Audio-2
on its channels 1 and 2 and a RackPy module running rackpy.AudioInterface on
channels 3 and 4 (the port's offsets), each module patched to itself, its
outputs to its inputs. The device must get the same samples back on both
pairs, frame for frame: whichever module is Rack's master, on 1 or 4 engine
threads, and resampled when the engine's rate is fixed below the device's.
The RackPy port times its frames exactly as Core Audio does.
"""

import gc
import importlib
import random
from array import array
from collections.abc import Iterator
from typing import Any

import pytest
from _rackpy_host import _testing as HOOKS

from rackpy import audio as fa
from rackpy import raw
from rackpy.errors import OwnershipError
from rackpy.raw import audio
from rackpy.raw import settings as rset
from rackpy.raw.engine import Cable, Engine, EngineModule, Module

runner: Any = importlib.import_module("rackpy_in_rack_pytest")

AUDIO_ID = 4351
BLOCK = 256
STATE: dict[str, Any] = {}


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def patch_to_itself(module: Module | EngineModule) -> list[Cable]:
    cables = []
    for i in (0, 1):
        c = Cable()
        c.output_module, c.output_id, c.input_module, c.input_id = module, i, module, i
        engine().add_cable(c)
        cables.append(c)
    return cables


def interface(port: audio.Port) -> Module:
    runner.port = port
    model = raw.plugin.get_model("RackPy", "IOProbe")
    assert model is not None
    try:
        module = model.create_module()
    finally:
        runner.port = None
    assert isinstance(module, Module)
    return module


@pytest.fixture(scope="module")
def rig() -> Iterator[dict[str, Any]]:
    """The device, Core's Audio-2 on channels 1-2, and the RackPy module on channels 3-4, both in Rack's engine."""
    e = engine()
    driver = audio.PythonDriver("RP-31J Audio")
    device = driver.add_device("IO 4", 4, 4, [44100, 48000], 48000, [64, 128, 256], BLOCK)
    audio.add_driver(AUDIO_ID, driver)
    model = raw.plugin.get_model("Core", "AudioInterface2")
    assert model is not None
    core = model.create_module()
    assert isinstance(core, EngineModule)
    e.add_module(core)
    saved = {"driver": AUDIO_ID, "deviceName": "IO 4", "sampleRate": 48000.0, "blockSize": BLOCK}
    data = {"audio": saved, "dcFilter": False}
    e.module_from_json(core, {"plugin": "Core", "model": "AudioInterface2", "data": data})
    port: audio.Port = fa.open_port(AUDIO_ID, "IO 4")
    port.input_offset = port.output_offset = 2
    module = interface(port)
    e.add_module(module)
    # Test-only cables are removed before the foreign reference module is deleted.
    core_cables = [HOOKS.engine_reference_loopback(core.id, i) for i in (0, 1)]
    cables = patch_to_itself(module)
    rig = {"driver": driver, "device": device, "core": core, "port": port, "module": module, "cables": cables}
    STATE.update(rig)
    yield rig
    for c in cables:
        e.remove_cable(c)
    for m in (module, core):
        if e.get_master_module() == m:
            e.set_master_module(None)
    e.remove_module(module)
    for cable_id in core_cables:
        HOOKS.engine_disconnect(cable_id)
    e.remove_module(core)


def blocks(device: audio.PythonDevice, count: int, seed: int) -> list[tuple[list[float], list[float]]]:
    """`count` blocks of noise past full scale, the same on both pairs; each block's pairs as the device got them."""
    rng = random.Random(seed)
    stream = fa.Stream(device)
    got = []
    for _ in range(count):
        data = array("f", bytes(4 * 4 * BLOCK))
        for i in range(BLOCK):
            a, b = rng.uniform(-1.2, 1.2), rng.uniform(-1.2, 1.2)
            data[4 * i : 4 * i + 4] = array("f", [a, b, a, b])
        out = stream.process(BLOCK, data)
        pairs = [[out[4 * i + c] for i in range(BLOCK) for c in channels] for channels in ((0, 1), (2, 3))]
        got.append((pairs[0], pairs[1]))
    return got


def stats_of(m: Module) -> dict[str, int]:
    stats = m.audio_stats
    assert stats is not None
    return stats


def same_as_core(got: list[tuple[list[float], list[float]]]) -> bool:
    return all(core == ours for core, ours in got) and any(any(x != 0.0 for x in core) for core, _ in got)


def test_a_python_port_becomes_a_modules_audio_port(rig: dict[str, Any]) -> None:
    module, port = rig["module"], rig["port"]
    assert module.audio_port is port and port.max_inputs == 2 and port.max_outputs == 2
    stats = module.audio_stats
    assert stats is not None and stats["inputs"] == 2 and stats["outputs"] == 2
    with pytest.raises(OwnershipError, match="channels"):
        port.max_inputs = 4  # the rings have their sizes
    with pytest.raises(OwnershipError, match="constructor callback"):
        module.set_audio_port(None)  # Rack's engine runs the module
    driver_view = audio.get_driver(AUDIO_ID)
    assert driver_view is not None
    with pytest.raises(OwnershipError, match="only its own device"):
        driver_view.subscribe(1, port)
    with pytest.raises(OwnershipError, match="another module"):
        interface(port)


def test_a_port_that_overrides_process_is_refused() -> None:
    class Overriding(audio.Port):
        def process_buffer(self, *args: Any) -> None:
            pass

    port = Overriding()
    with pytest.raises(TypeError, match="process_buffer"):
        interface(port)

    def no_channels(m: Module) -> None:
        m.config(0, 0, 0, 0)
        m.set_audio_port(audio.Port(), inputs=0, outputs=0)

    with pytest.raises(ValueError, match="at least one channel"):
        Module.create(no_channels)


@pytest.mark.parametrize("master", ["core", "module"])
@pytest.mark.parametrize("threads", [1, 4])
def test_the_device_hears_the_same_as_through_core_audio(rig: dict[str, Any], master: str, threads: int) -> None:
    e, module = engine(), rig["module"]
    previous = rset.settings.thread_count
    rset.settings.thread_count = threads
    try:
        e.set_master_module(rig[master])
        before = module.audio_stats
        assert before is not None
        got = blocks(rig["device"], 24, seed=threads)
        after = module.audio_stats
        assert after is not None
        assert e.get_master_module() == rig[master]
        assert same_as_core(got)
        assert after["blocks"] - before["blocks"] == 24 and after["resampler_misses"] == 0
        stepped = after["stepped_frames"] - before["stepped_frames"]
        assert stepped == (24 * BLOCK if master == "module" else 0)  # a master steps Rack's engine by each block
    finally:
        rset.settings.thread_count = previous


def test_either_module_claims_a_free_master(rig: dict[str, Any]) -> None:
    e = engine()
    e.set_master_module(None)
    got = blocks(rig["device"], 8, seed=7)
    assert e.get_master_module() in (rig["core"], rig["module"]) and same_as_core(got[1:])


def test_a_fixed_engine_rate_resamples_as_core_audio_does(rig: dict[str, Any]) -> None:
    e, module = engine(), rig["module"]
    e.set_master_module(rig["module"])
    rset.settings.sample_rate = 44100.0
    try:
        HOOKS.engine_set_sample_rate(44100.0)  # Rack's settings win over the device's suggestion
        assert e.get_sample_rate() == 44100.0
        HOOKS.maintain_audio_ports()  # a UI step: converters for 48 kHz to 44.1 kHz and back
        before = module.audio_stats
        assert before is not None
        got = blocks(rig["device"], 30, seed=3)
        after = module.audio_stats
        assert after is not None
        assert same_as_core(got) and after["resampler_misses"] == before["resampler_misses"]
        # A rate the main thread has not seen yet: silent blocks, counted, until its next UI step.
        rset.settings.sample_rate = 96000.0
        HOOKS.engine_set_sample_rate(96000.0)
        missed = blocks(rig["device"], 2, seed=4)
        stats = module.audio_stats
        assert stats is not None and stats["resampler_misses"] > after["resampler_misses"]
        assert all(x == 0.0 for _, ours in missed for x in ours)
        HOOKS.maintain_audio_ports()
        later = blocks(rig["device"], 4, seed=5)
        assert any(x != 0.0 for _, ours in later[1:] for x in ours)
    finally:
        rset.settings.sample_rate = 0.0
        HOOKS.engine_set_sample_rate(48000.0)
        HOOKS.maintain_audio_ports()


def test_the_port_going_first_leaves_the_module_silent(rig: dict[str, Any]) -> None:
    e = engine()
    port: audio.Port = fa.open_port(AUDIO_ID, "IO 4")
    port.input_offset = port.output_offset = 2
    second = interface(port)
    second.set_audio_port(None)  # removed while Python owns it
    assert second.audio_port is None and second.audio_stats is None
    port.max_inputs = 4  # an ordinary port again
    second.set_audio_port(port)
    e.add_module(second)
    e.set_master_module(rig["core"])
    blocks(rig["device"], 2, seed=8)
    assert stats_of(second)["blocks"] == 2
    del port
    gc.collect()  # the port goes, while Rack's engine runs the module: it hears silence
    assert second.audio_port is None
    blocks(rig["device"], 2, seed=9)
    assert stats_of(second)["blocks"] == 2
    e.remove_module(second)
    second.close()
