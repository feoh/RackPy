# SPDX-License-Identifier: GPL-3.0-or-later
"""Owned quantities borrow a Module without becoming its engine-owned metadata."""

import gc
import threading
import weakref
from collections.abc import Callable

import _rackpy_host
import pytest

from rackpy.errors import ContextError, OwnershipError, StaleHandleError, ThreadError
from rackpy.raw import Quantity
from rackpy.raw.engine import Module, ParamQuantity, SwitchQuantity


def owner_of(q: ParamQuantity) -> Module | None:
    return q.module  # native teardown changes this independently of Python assignments


def make_module() -> Module:
    return Module.create(lambda m: m.config(2, 0, 0))


def value_actions(q: ParamQuantity) -> list[Callable[[], object]]:
    return [
        q.get_value,
        lambda: q.set_value(0.5),
        q.get_immediate_value,
        lambda: q.set_immediate_value(0.5),
        q.get_display_value,
        lambda: q.set_display_value(0.5),
        q.get_display_value_string,
        lambda: q.set_display_value_string("0.5"),
        q.get_string,
        q.reset,
        q.randomize,
        q.is_min,
        q.is_max,
        q.set_min,
        q.set_max,
        q.toggle,
        lambda: q.move_value(0.5),
        q.get_scaled_value,
        lambda: q.set_scaled_value(0.5),
        lambda: q.move_scaled_value(0.5),
    ]


@pytest.mark.parametrize("quantity_class", [ParamQuantity, SwitchQuantity])
def test_attached_identity_null_errors_and_threads(quantity_class: type[ParamQuantity]) -> None:
    q = quantity_class()
    first, second, closed = make_module(), make_module(), make_module()
    closed.close()
    try:
        q.module = first
        for index in (0, 1):
            q.param_id = index
            assert q.get_param() is first.params[index]
        q.param_id = -1
        assert q.get_param() is None  # preserve the SDK's raw invalid-ID null result
        for index in (-1, 2, -(2**31), 2**31 - 1):
            q.param_id = index
            assert q.get_param() is None
            for action in value_actions(q):
                with pytest.raises(IndexError):
                    action()  # never let Engine's unchecked vector access see the ID
        q.param_id = 0
        with pytest.raises(TypeError):
            q.module = object()  # type: ignore[assignment]
        with pytest.raises(StaleHandleError):
            q.module = closed
        assert q.module is first
        q.module = second
        first.close()
        assert q.module is second and q.get_param() is second.params[0]
        assert id(second.param_quantities[0]) != id(q)  # no slot ownership transfer
        actions: list[Callable[[], object]] = [
            *value_actions(q),
            lambda: q.module,
            q.get_param,
            lambda: setattr(q, "module", None),
        ]
        failures: list[BaseException] = []

        def worker() -> None:
            for action in actions:
                try:
                    action()
                except BaseException as error:
                    failures.append(error)

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(failures) == len(actions) and all(isinstance(e, ThreadError) for e in failures)
        q.module = None
        second.close()
        assert owner_of(q) is None and q.get_param() is None
        assert q.get_value() == 0
    finally:
        first.close()
        second.close()


@pytest.mark.parametrize("quantity_class", [ParamQuantity, SwitchQuantity])
def test_attached_rollback_gc_reassignment_and_churn(quantity_class: type[ParamQuantity]) -> None:
    q = quantity_class()
    q.param_id = 0
    retained = []

    def fail(m: Module) -> None:
        m.config(1, 0, 0)
        q.module = m
        retained.append(q.get_param())
        raise ValueError("rollback link")

    with pytest.raises(ValueError, match="rollback link"):
        Module.create(fail)
    assert owner_of(q) is None and q.get_param() is None
    with pytest.raises(StaleHandleError):
        assert retained[0] is not None
        _ = retained[0].value
    seen: weakref.WeakSet[Module] = weakref.WeakSet()
    survivor = make_module()
    try:
        for _ in range(1000):
            m = make_module()
            seen.add(m)
            q.module = m
            param = q.get_param()
            del m
            assert owner_of(q) is None and q.get_value() == 0
            assert param is not None
            with pytest.raises(StaleHandleError):
                _ = param.value
            q.module = survivor
            pending = [quantity_class()]
            pending[0].module = survivor
            # A worker may drop the last quantity reference while the main
            # thread deletes its borrowed Module. Native detach synchronizes.
            thread = threading.Thread(target=pending.clear)
            thread.start()
            thread.join()
        gc.collect()
        assert not seen and q.module is survivor
    finally:
        survivor.close()
    assert owner_of(q) is None and q.get_value() == 0


def test_attached_native_reference_or_missing_context() -> None:
    if _rackpy_host.bound_context_generation() is None:
        q = ParamQuantity()
        m = make_module()
        try:
            q.module, q.param_id = m, 0
            assert q.get_param() is m.params[0]
            assert q.get_label() == "#1" and q.get_min_value() == 0
            for action in value_actions(q):
                with pytest.raises(ContextError, match="live bound"):
                    action()
        finally:
            m.close()
        return

    from _rackpy_host import _testing

    for case in _testing.quantity_reference():
        m = make_module()
        q, switch = ParamQuantity(), SwitchQuantity()
        try:
            q.module, q.param_id = m, 0
            q.min_value, q.max_value, q.default_value = 1, 8, 2
            q.name, q.unit = "Pitch", " Hz"
            q.display_base, q.display_multiplier, q.display_offset = case["base"], 3, 4
            for x, value, display, text in case["values"]:
                q.set_value(x)
                assert q.get_value() == q.get_immediate_value() == m.params[0].value == pytest.approx(value)
                assert q.get_display_value() == pytest.approx(display)
                assert q.get_display_value_string() == text
            for display, expected in case["displays"]:
                q.set_display_value(display)
                assert q.get_immediate_value() == pytest.approx(expected)
            q.set_display_value_string("2 + 8")
            assert q.get_immediate_value() == pytest.approx(case["parsed"])
            q.reset()
            assert q.get_value() == case["reset"]
            for method, arg, value, scaled, is_min, is_max, text in case["helpers"]:
                action = getattr(q, method)
                action(arg) if method in {"move_value", "set_scaled_value", "move_scaled_value"} else action()
                assert q.get_value() == pytest.approx(value)
                assert q.get_scaled_value() == pytest.approx(scaled)
                assert [q.is_min(), q.is_max(), q.get_string()] == [is_min, is_max, text]
            q.smooth_enabled = True
            for action in (
                lambda quantity=q: quantity.set_value(3),
                q.set_min,
                q.set_max,
                q.toggle,
                lambda quantity=q: quantity.move_value(1),
                lambda quantity=q: quantity.set_scaled_value(0.5),
                lambda quantity=q: quantity.move_scaled_value(0.5),
            ):
                with pytest.raises(OwnershipError, match="engine-owned"):
                    action()
            q.set_immediate_value(3)
            q.set_display_value(10)
            q.reset()  # direct quantity reset ignores module-level enable flags
            assert q.get_value() == 2
            with pytest.raises(ContextError):
                _testing.with_foreign_context(q.get_value)
            switch.module, switch.param_id = m, 1
            switch.min_value, switch.max_value = -1, 1
            switch.snap_enabled = True
            switch.labels = ["Low", "Middle", "High"]
            for x, value, text in case["switches"]:
                switch.set_immediate_value(x)
                assert [switch.get_value(), switch.get_display_value_string()] == [value, text]
            switch.set_display_value_string("mIDdle")
            assert switch.get_value() == case["switch_parsed"]
            for _ in range(100):
                q.randomize()
                switch.randomize()
                assert 1 <= q.get_value() <= 8 and switch.get_value() in (-1, 0, 1)
        finally:
            m.close()
        assert owner_of(q) is owner_of(switch) is None
        assert q.get_value() == switch.get_value() == 0


def test_attached_ui_override_reentry_and_base_errors() -> None:
    m = make_module()

    class Closing(ParamQuantity):
        def get_label(self) -> str:
            m.close()
            return super().get_label() + " detached"

        def get_description(self) -> str:
            raise ValueError("callback failed")

    q = Closing()
    q.module, q.param_id = m, 0
    with pytest.raises(ValueError, match="callback failed"):
        ParamQuantity.get_description(q)
    assert q.module is m
    assert Quantity.get_label(q) == "#1 detached"
    assert owner_of(q) is None and q.get_value() == 0

    if _rackpy_host.bound_context_generation() is not None:
        m = make_module()

        class ClosingDisplay(SwitchQuantity):
            def set_display_value(self, value: float) -> None:
                m.close()
                super().set_display_value(value)  # null-module native fallback

        display = ClosingDisplay()
        display.module, display.param_id = m, 0
        Quantity.set_display_value(display, 0.5)
        assert owner_of(display) is None and display.get_value() == 0
