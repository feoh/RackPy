# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-15A: module and quantity JSON, and constructor-set data handlers."""

import gc
import threading
import weakref
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest

from rackpy.errors import ContextError, OwnershipError, ThreadError
from rackpy.raw.engine import Module, ParamQuantity, Port, PortInfo

LIVE = _rackpy_host.bound_context_generation() is not None


def with_handlers(state: dict[str, Any]) -> Module:
    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 2)
        m.set_data_handlers(to_json=lambda: dict(state), from_json=lambda data: state.update(loaded=data))

    return Module.create(build)


def test_handlers_are_constructor_only_and_callable() -> None:
    def build(m: Module) -> None:
        m.config(0, 0, 0)
        with pytest.raises(TypeError, match="callable"):
            m.set_data_handlers(to_json=42)  # type: ignore[arg-type]
        m.set_data_handlers(to_json=lambda: {"first": 1})
        m.set_data_handlers(to_json=lambda: {"second": 2})  # replaces
        with pytest.raises(OwnershipError, match="after the constructor"):
            m.data_to_json()

    m = Module.create(build)
    try:
        assert m.data_to_json() == {"second": 2}
        with pytest.raises(OwnershipError, match="constructor callback"):
            m.set_data_handlers()
    finally:
        m.close()

    def cleared(m: Module) -> None:
        m.set_data_handlers(to_json=lambda: {"x": 1})
        m.set_data_handlers()  # both None: Rack's defaults again

    m = Module.create(cleared)
    try:
        assert m.data_to_json() is None
        m.data_from_json({"ignored": True})  # Rack's default does nothing
    finally:
        m.close()


def test_data_handlers_round_trip_and_follow_rack_rules() -> None:
    state: dict[str, Any] = {"mode": 3, "name": "λ / 𝄞", "nested": [1, 2.5, None, True]}
    m = with_handlers(state)
    try:
        assert m.data_to_json() == {"mode": 3, "name": "λ / 𝄞", "nested": [1, 2.5, None, True]}
        m.data_from_json({"mode": 4})
        assert state["loaded"] == {"mode": 4}
        m.data_from_json(None)  # Rack passes JSON null through: the handler sees None
        assert state["loaded"] is None
    finally:
        m.close()
    empty = Module.create(lambda m: m.set_data_handlers(to_json=lambda: None))
    try:
        assert empty.data_to_json() is None  # None means "no data", as Rack's NULL
    finally:
        empty.close()


@pytest.mark.parametrize(
    ("value", "error"),
    [
        (float("nan"), ValueError),
        ({1: "key"}, TypeError),
        (2**64, OverflowError),
        ("\ud800", UnicodeError),
        (object(), TypeError),
    ],
)
def test_handler_results_follow_the_json_policy(value: object, error: type[Exception]) -> None:
    m = Module.create(lambda m: m.set_data_handlers(to_json=lambda: {"value": value}))
    try:
        with pytest.raises(error):
            m.data_to_json()
    finally:
        m.close()


def test_handler_errors_reach_a_python_caller() -> None:
    def fail() -> dict[str, Any]:
        raise RuntimeError("to_json failed")

    def fail_load(data: object) -> None:
        raise KeyError("from_json failed")

    m = Module.create(lambda m: m.set_data_handlers(to_json=fail, from_json=fail_load))
    try:
        with pytest.raises(RuntimeError, match="to_json failed"):
            m.data_to_json()
        with pytest.raises(KeyError, match="from_json failed"):
            m.data_from_json({})
        deep: Any = []
        for _ in range(100):
            deep = [deep]
        with pytest.raises(ValueError, match="nested deeper"):
            m.data_from_json(deep)
    finally:
        m.close()


def test_module_json_needs_a_model_and_params_need_rack() -> None:
    def build(m: Module) -> None:
        m.config(2, 0, 0, 0)
        m.config_param(1, -1, 1, 0.5)

    m = Module.create(build)
    try:
        modelled: list[Callable[[], object]] = [m.to_json, lambda: m.from_json({})]
        for action in modelled:
            with pytest.raises(OwnershipError, match="RP-16"):
                action()
        if not LIVE:
            contextual: list[Callable[[], object]] = [m.params_to_json, lambda: m.params_from_json([])]
            for action in contextual:
                with pytest.raises(ContextError, match="live bound"):
                    action()
            return
        m.params[0].value = 0.25
        assert m.params_to_json() == [{"value": 0.25, "id": 0}, {"value": 0.5, "id": 1}]
        m.params_from_json([{"id": 1, "value": -0.75}, {"id": 9, "value": 1}, {"value": 0.5}])
        # Rack's rules: unknown IDs are skipped; an entry without an ID uses its index.
        assert [p.value for p in m.params] == [0.25, -0.75]
        m.params_from_json([{"value": 0.5}, {"paramId": 0, "value": 0.75}])
        assert [p.value for p in m.params] == [0.75, -0.75]
    finally:
        m.close()


def test_quantity_json_overrides() -> None:
    calls: list[object] = []

    class Tagged(ParamQuantity):
        def to_json(self) -> dict[str, Any]:
            return {"value": self.get_value(), "tag": "kept"}

        def from_json(self, data: dict[str, Any]) -> None:
            calls.append(data)
            super().from_json(data)

    class Broken(ParamQuantity):
        def to_json(self) -> dict[str, Any]:
            raise ValueError("quantity to_json failed")

    def build(m: Module) -> None:
        m.config(2, 0, 0, 0)
        m.config_param(0, 0, 10, 4, quantity_type=Tagged)
        m.config_param(1, 0, 10, 6, quantity_type=Broken)

    m = Module.create(build)
    try:
        tagged, broken = m.get_param_quantity(0), m.get_param_quantity(1)
        assert isinstance(tagged, Tagged) and isinstance(broken, Broken)
        if not LIVE:
            with pytest.raises(ContextError):
                tagged.to_json()
            return
        assert tagged.to_json() == {"value": 4, "tag": "kept"}
        tagged.from_json({"value": 7})
        assert calls == [{"value": 7}] and m.params[0].value == 7
        assert ParamQuantity.to_json(tagged) == {"value": 7, "tag": "kept"}
        with pytest.raises(ValueError, match="quantity to_json failed"):
            m.params_to_json()  # Rack's paramsToJson reached the failing override
        with pytest.raises(ValueError, match="quantity to_json failed"):
            broken.to_json()
    finally:
        m.close()


def test_cycles_through_the_module_are_collected() -> None:
    class Owning(PortInfo):
        owner: Module | None = None

    def build(m: Module) -> None:
        m.config(0, 1, 0, 0)
        info = Owning()
        info.owner = m  # a subclass attribute that refers back to the module
        info.module, info.port_id, info.type = m, 0, Port.Type.INPUT
        m.input_infos[0] = info
        m.set_data_handlers(to_json=lambda: {"inputs": m.get_num_inputs()})  # a closure over the module

    tracked: list[weakref.ref[Module]] = []
    for _ in range(100):
        m = Module.create(build)
        assert m.data_to_json() == {"inputs": 1}
        tracked.append(weakref.ref(m))
        del m
    gc.collect()
    assert all(ref() is None for ref in tracked)


def test_json_main_thread_rule() -> None:
    m = with_handlers({"x": 1})
    actions: list[Callable[[], object]] = [m.data_to_json, lambda: m.data_from_json({}), m.params_to_json]
    errors: list[BaseException] = []

    def worker() -> None:
        for action in actions:
            try:
                action()
            except BaseException as error:
                errors.append(error)

    try:
        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(errors) == len(actions) and all(isinstance(e, ThreadError) for e in errors)
    finally:
        m.close()
