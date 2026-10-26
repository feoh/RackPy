# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-24B: cables and parameter handles in Rack's live UI with a running engine.

The RuntimeProbe is a C++ module with one param, one input and one output, so
it is an EngineModule: a module RackPy did not create.
"""

import warnings
from collections.abc import Iterator

import pytest

from rackpy import raw
from rackpy.errors import EngineLockError, OwnershipError
from rackpy.raw.engine import Cable, Engine, EngineModule, Module, ParamHandle

SENTINEL_ID = 2**53


def engine() -> Engine:
    context = raw.context_get()
    assert context is not None and context.engine is not None
    return context.engine


def registered(h: ParamHandle) -> bool:
    return h.in_engine  # a call, so that type checkers do not narrow it between engine calls


@pytest.fixture
def probe() -> Iterator[EngineModule]:
    found = [engine().get_module(i) for i in engine().get_module_ids()]
    probes = [
        m for m in found if isinstance(m, EngineModule) and m.model is not None and m.model.slug == "RuntimeProbe"
    ]
    assert probes
    yield probes[0]


def test_a_cable_python_makes(probe: EngineModule) -> None:
    c = Cable()
    assert not c.in_engine and c.alive and c.id == -1 and c.input_module is None and c.input_id == -1
    assert c.output_module is None and c.output_id == -1
    with pytest.raises(ValueError, match="both"):
        c.to_json()  # Rack's toJson reads both modules
    with pytest.raises(ValueError):
        c.id = 2**53
    with pytest.raises(TypeError):
        c.input_module = 3  # type: ignore[assignment]
    c.output_module, c.output_id, c.input_module, c.input_id = probe, 0, probe, 0
    assert c.to_json() == {"id": -1, "outputModuleId": probe.id, "outputId": 0, "inputModuleId": probe.id, "inputId": 0}
    loaded = Cable()
    loaded.from_json({"id": 12, "outputModuleId": probe.id, "outputId": 0, "inputModuleId": probe.id, "inputId": 0})
    assert loaded.id == 12 and loaded.input_module == probe and loaded.output_module == probe
    with pytest.raises(ValueError, match="not found"):
        Cable().from_json({"outputModuleId": -5, "outputId": 0, "inputModuleId": probe.id, "inputId": 0})
    assert c != loaded and len({c, loaded}) == 2


def test_the_engines_cables(probe: EngineModule) -> None:
    e = engine()
    assert e.get_num_cables() == len(e.get_cable_ids())
    assert e.get_cable(-5) is None and not e.has_cable(Cable())
    c = Cable()
    c.output_module, c.output_id, c.input_module, c.input_id = probe, 0, probe, 0
    with pytest.raises(OwnershipError, match="another plugin's"):
        e.add_cable(c)  # Rack asserts when a module widget deletes a module a widgetless cable touches
    with pytest.raises(OwnershipError, match="does not have"):
        e.remove_cable(c)
    detached = Module.create(lambda m: m.config(0, 1, 1, 0))
    try:
        c.input_module = detached
        with pytest.raises(OwnershipError):
            e.add_cable(c)  # the output is still the probe
    finally:
        detached.close()


def test_a_param_handle_python_makes(probe: EngineModule) -> None:
    e = engine()
    h = ParamHandle()
    assert h.ours and not registered(h) and h.module_id == -1 and h.param_id == 0 and h.text == "" and h.module is None
    h.text = "Cutoff"
    h.color = raw.nanovg.rgb(255, 128, 0)
    e.add_param_handle(h)
    try:
        assert registered(h)
        with pytest.raises(OwnershipError, match="already has"):
            e.add_param_handle(h)
        with pytest.raises(OwnershipError, match="update_param_handle"):
            h.module_id = probe.id  # Rack: use the engine's methods
        e.update_param_handle(h, probe.id, 0)
        assert (h.module_id, h.param_id) == (probe.id, 0) and h.module == probe
        assert e.get_param_handle(probe.id, 0) == h
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            assert e.get_param_handle(probe, 0) == h  # Rack's deprecated overload
        assert any(issubclass(w.category, DeprecationWarning) for w in caught)
        with pytest.raises(ValueError, match="out of range"):
            e.update_param_handle(h, probe.id, 1)  # Rack indexes the param unchecked
        with pytest.raises(ValueError, match="below 2"):
            e.update_param_handle(h, SENTINEL_ID, -1)
        assert e.get_param_handle(SENTINEL_ID, -1) is None  # RackPy's sentinel is hidden

        other = ParamHandle()
        e.add_param_handle(other)
        try:
            e.update_param_handle(other, probe.id, 0, overwrite=False)
            assert other.module_id == -1 and h.module_id == probe.id  # Rack unmaps the newcomer
            e.update_param_handle(other, probe.id, 0)
            assert other.module_id == probe.id and h.module_id == -1  # Rack unmaps the old handle
            assert e.get_param_handle(probe.id, 0) == other
            e.update_param_handle(other, probe.id, 0)
            assert other.module_id == -1  # Rack 2.6.6: re-mapped to its own param, it finds itself and is unmapped
            assert e.get_param_handle(probe.id, 0) is None
        finally:
            e.remove_param_handle(other)
    finally:
        e.remove_param_handle(h)
    assert not registered(h) and h.text == "Cutoff"
    with pytest.raises(OwnershipError, match="does not have"):
        e.remove_param_handle(h)
    with pytest.raises(OwnershipError, match="does not have"):
        e.update_param_handle(h, probe.id, 0)
    h.module_id = 4
    with pytest.raises(ValueError, match="blank"):
        e.add_param_handle(h)  # Rack's addParamHandle asserts on a mapped handle


def test_no_lock_calls_need_the_lock(probe: EngineModule) -> None:
    e = engine()
    h = ParamHandle()
    for call in (
        lambda: e.get_module_no_lock(probe.id),
        lambda: e.set_master_module_no_lock(None),
        lambda: e.get_param_handle_no_lock(probe.id, 0),
        lambda: e.update_param_handle_no_lock(h, -1, 0),
    ):
        with pytest.raises(EngineLockError, match="lock held"):
            call()
