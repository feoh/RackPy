# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24A: Rack's engine through rackpy.raw.engine.Engine, in Rack's live UI with a running engine.

The RuntimeProbe is a C++ module (a test hook), so it is an EngineModule:
a module RackPy did not create.
"""

import math
import time
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest

from rackpy import raw
from rackpy.errors import OwnershipError
from rackpy.raw.engine import Engine, EngineModule, Module


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


@pytest.fixture
def probe() -> Iterator[EngineModule]:
    ids = engine().get_module_ids()
    found = [engine().get_module(i) for i in ids]
    probes = [
        m for m in found if isinstance(m, EngineModule) and m.model is not None and m.model.slug == "RuntimeProbe"
    ]
    assert probes
    yield probes[0]


def test_the_engine_and_its_timing() -> None:
    e = engine()
    assert e is engine() and e.alive
    rate = e.get_sample_rate()
    assert rate > 0 and e.get_sample_time() == pytest.approx(1 / rate)
    before = e.get_frame()
    time.sleep(0.05)  # Rack's own thread steps the engine (no audio device here)
    assert e.get_frame() > before
    assert e.get_block() >= 0 and e.get_block_frame() >= 0 and e.get_block_frames() >= 0
    assert math.isfinite(e.get_block_time()) and e.get_block_duration() >= 0
    assert e.get_meter_average() >= 0 and e.get_meter_max() >= 0
    for bad in (0, 1 << 20):
        with pytest.raises(ValueError):
            e.step_block(bad)
    with pytest.raises(OwnershipError, match="master module"):
        e.step_block(64)  # Rack: only the master module steps the engine
    e.yield_workers()
    e.set_suggested_sample_rate(48000)
    try:
        assert e.get_sample_rate() == 48000
    finally:
        e.set_suggested_sample_rate(rate)
    with pytest.raises(ValueError):
        e.set_suggested_sample_rate(math.nan)


def test_modules_in_the_engine(probe: EngineModule) -> None:
    e = engine()
    assert probe.id in e.get_module_ids() and e.get_num_modules() >= 1
    assert e.get_module(probe.id) == probe and hash(e.get_module(probe.id)) == hash(probe)
    assert e.has_module(probe) and probe.alive and probe.get_num_params() == 1
    assert e.get_module(-12345) is None
    data = e.module_to_json(probe)
    assert data["model"] == "RuntimeProbe" and data["id"] == probe.id
    e.module_from_json(probe, data)
    e.prepare_save_module(probe)
    e.prepare_save()
    e.bypass_module(probe, True)
    assert probe.is_bypassed()
    e.bypass_module(probe, False)
    assert not probe.is_bypassed()
    e.randomize_module(probe)
    e.reset_module(probe)
    patch = e.to_json()
    assert probe.id in [m["id"] for m in patch["modules"]]


def test_params(probe: EngineModule) -> None:
    e = engine()
    value = e.get_param_value(probe, 0)
    try:
        e.set_param_value(probe, 0, 0.25)
        assert e.get_param_value(probe, 0) == 0.25
        e.set_param_smooth_value(probe, 0, 0.75)  # Rack smooths toward it as the engine steps
        assert e.get_param_smooth_value(probe, 0) == 0.75
        with pytest.raises(ValueError, match="out of range"):
            e.get_param_value(probe, 1)  # Rack indexes params unchecked
    finally:
        e.set_param_value(probe, 0, value)
    detached = Module.create(lambda m: m.config(1, 0, 0, 0))
    try:
        e.set_param_value(detached, 0, 0.5)
        assert e.get_param_value(detached, 0) == 0.5
        with pytest.raises(OwnershipError, match="not in Rack's engine"):
            e.set_param_smooth_value(detached, 0, 1)  # Rack would keep its raw pointer
    finally:
        detached.close()


def test_the_master_module(probe: EngineModule) -> None:
    e = engine()
    master = e.get_master_module()
    try:
        e.set_master_module(probe)
        assert e.get_master_module() == probe
    finally:
        e.set_master_module(master)
    assert e.get_master_module() == master


def test_what_the_engine_refuses(probe: EngineModule) -> None:
    e = engine()
    with pytest.raises(OwnershipError, match="clear the rack"):
        e.clear()  # it deletes modules Rack's widgets own
    with pytest.raises(OwnershipError, match="clear the rack"):
        e.from_json(e.to_json())
    with pytest.raises(OwnershipError, match="widget owns it"):
        e.remove_module(probe)  # another plugin's module leaves with its widget
    modelless = Module.create(lambda m: m.config(0, 0, 0, 0))
    try:
        with pytest.raises(OwnershipError, match="model"):
            e.add_module(modelless)  # Rack saves a module with its model and plugin
    finally:
        modelless.close()


def test_runnable_engine_snippet() -> None:
    path = Path(__file__).resolve().parents[4] / "docs" / "snippets" / "engine.py"
    scope: dict[str, Any] = {}
    exec(compile(path.read_text(), str(path), "exec"), scope)
    lines = scope["describe_engine"]()
    assert lines[0].endswith("modules") and any(line.startswith("RuntimeProbe ") for line in lines[1:])
