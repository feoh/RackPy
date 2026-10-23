# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14C2: Rack's engine owns and deletes RackPy modules (live UI suite only).

Test hooks play Rack's part until RP-16's Model::createModule: adopting a
sealed module into APP->engine, and removing and deleting it as
ModuleWidget::setModule(NULL) does. The production start-up run ignores this
file: it has neither the hooks nor an engine.
"""

import gc
import threading
import time
import weakref
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import Module, ParamQuantity, Port, PortInfo


def build(configure: Callable[[Module], object] | None = None) -> Module:
    def run(m: Module) -> None:
        m.config(2, 1, 1, 1)
        if configure:
            configure(m)

    return Module.create(run)


def alive(handle: Any) -> bool:
    return bool(handle.alive)  # Rack's deletion changes this behind mypy's narrowing


class Labelled(ParamQuantity):
    def get_label(self) -> str:
        return "Labelled"


def test_adoption_keeps_identity_and_refuses_python_deletion() -> None:
    m = build()
    param, quantity, inputs = m.params[0], m.get_param_quantity(0), m.input_infos
    module_id = HOOKS.engine_adopt(m)
    try:
        assert m.id == m.get_id() == module_id and 0 <= module_id < 2**53
        assert HOOKS.engine_has_module(module_id) and m.alive
        assert param.alive and quantity.alive and inputs[0].alive
        assert m.params[0] is param and m.get_param_quantity(0) is quantity
        with pytest.raises(OwnershipError, match="Rack deletes it"):
            m.close()
        assert m.alive and HOOKS.engine_has_module(module_id)
        with pytest.raises(OwnershipError, match="already owns"):
            HOOKS.engine_adopt(m)
        with pytest.raises(OwnershipError, match="constructor callback"):
            m.config_param(0, 0, 1, 0)
    finally:
        HOOKS.engine_delete(m)
    assert not alive(m) and not HOOKS.engine_has_module(module_id)
    m.close()  # idempotent once Rack has deleted it


def test_rack_deletion_invalidates_handles_views_and_transferred_objects() -> None:
    made: dict[str, object] = {}

    def configure(m: Module) -> None:
        made["quantity"] = m.config_param(1, 0, 1, 0, "Q", quantity_type=Labelled)
        info = PortInfo()
        info.module, info.port_id, info.type = m, 0, Port.Type.OUTPUT
        m.output_infos[0] = info
        made["info"] = info

    baseline = _rackpy_host.transferred_objects()
    m = build(configure)
    views: list[Any] = [m.params[0], m.inputs[0], m.lights[0], m.get_param_quantity(0), m.get_input_info(0)]
    groups: list[Any] = [m.params, m.param_quantities]
    HOOKS.engine_adopt(m)
    assert _rackpy_host.transferred_objects() == baseline + 2
    HOOKS.engine_delete(m)  # without the GIL, as Rack deletes it
    assert not m.alive
    with pytest.raises(StaleHandleError, match="Rack has deleted"):
        m.get_num_params()
    for view in views:
        assert not view.alive
    for group in groups:
        with pytest.raises(StaleHandleError):
            len(group)
    quantity, info = made["quantity"], made["info"]
    assert isinstance(quantity, Labelled) and isinstance(info, PortInfo)
    assert not quantity.alive and not info.alive
    with pytest.raises(StaleHandleError):
        ParamQuantity.get_label(quantity)  # the bound method, not the pure-Python override
    assert _rackpy_host.transferred_objects() == baseline


def test_adoption_refusals() -> None:
    def adopt_early(m: Module) -> None:
        with pytest.raises(OwnershipError, match="after its constructor callback"):
            HOOKS.engine_adopt(m)

    m = build(adopt_early)
    with pytest.raises(OwnershipError, match="does not own"):
        HOOKS.engine_delete(m)
    with pytest.raises(OwnershipError, match="does not own"):
        HOOKS.engine_reset(m)
    m.close()
    with pytest.raises(StaleHandleError):
        HOOKS.engine_adopt(m)
    errors: list[BaseException] = []
    live = build()

    def worker() -> None:
        for action in (lambda: HOOKS.engine_adopt(live), lambda: HOOKS.engine_handle(0)):
            try:
                action()
            except BaseException as error:
                errors.append(error)

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    assert len(errors) == 2 and all(isinstance(e, ThreadError) for e in errors)
    assert live.alive and live.id == -1
    live.close()


def test_dropping_every_handle_leaves_rack_the_owner() -> None:
    m = build(lambda m: m.config_param(0, 0, 10, 4, "Kept"))
    module_id = HOOKS.engine_adopt(m)
    tracked = weakref.ref(m)
    del m
    gc.collect()
    assert tracked() is None and HOOKS.engine_has_module(module_id)
    again = HOOKS.engine_handle(module_id)
    assert isinstance(again, Module) and again.alive and again.id == module_id
    assert again.params[0].value == 4 and again is HOOKS.engine_handle(module_id)
    with pytest.raises(OwnershipError, match="Rack deletes it"):
        again.close()
    HOOKS.engine_delete(again)
    assert not alive(again) and HOOKS.engine_handle(module_id) is None


def test_smoothing_is_allowed_once_the_engine_owns_the_module() -> None:
    m = build(lambda m: m.config_param(0, 0, 10, 0, "Smooth"))
    quantity = m.get_param_quantity(0)
    attached = ParamQuantity()
    attached.module, attached.param_id = m, 1
    quantity.smooth_enabled = attached.smooth_enabled = True
    for target in (quantity, attached):
        with pytest.raises(OwnershipError, match="engine-owned"):
            target.set_value(5)
    HOOKS.engine_adopt(m)
    try:
        quantity.set_value(8)
        attached.set_value(0.5)
        # Rack's getValue returns the smoothing target at once...
        assert quantity.get_value() == 8 and attached.get_value() == 0.5
        # ...while the engine moves the Param towards it, block by block.
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and (m.params[0].value < 7.99 or m.params[1].value < 0.499):
            time.sleep(0.02)
        assert m.params[0].value == pytest.approx(8, abs=0.01) and m.params[1].value == pytest.approx(0.5, abs=0.01)
        with pytest.warns(DeprecationWarning):
            quantity.set_smooth_value(2)
        with pytest.warns(DeprecationWarning):
            assert quantity.get_smooth_value() == 2
    finally:
        HOOKS.engine_delete(m)
    assert attached.module is None


def test_engine_reset_reaches_python_quantity_overrides() -> None:
    calls: list[str] = []

    class Resetting(ParamQuantity):
        def reset(self) -> None:
            calls.append("reset")
            super().reset()

    m = build(lambda m: m.config_param(0, 0, 10, 3, quantity_type=Resetting))
    HOOKS.engine_adopt(m)
    try:
        m.params[0].value = 9
        HOOKS.engine_reset(m)  # Engine::resetModule, under Rack's exclusive lock
        assert calls == ["reset"] and m.params[0].value == 3
    finally:
        HOOKS.engine_delete(m)


def test_one_thousand_adoptions_and_rack_deletions() -> None:
    baseline = _rackpy_host.transferred_objects()
    owners: weakref.WeakSet[Module] = weakref.WeakSet()
    for _ in range(1000):
        m = build(lambda m: m.config_param(0, 0, 1, 0, quantity_type=Labelled))
        owners.add(m)
        HOOKS.engine_adopt(m)
        HOOKS.engine_delete(m)
        assert not m.alive
    del m
    gc.collect()
    assert not owners and _rackpy_host.transferred_objects() == baseline
