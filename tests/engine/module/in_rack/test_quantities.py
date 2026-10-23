# SPDX-License-Identifier: GPL-3.0-or-later
"""Checked built-in quantities, executed inside real Rack at startup and live UI."""

import gc
import math
import threading
import weakref
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest

from rackpy.errors import ContextError, OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import Module, ParamQuantity, ParamQuantityView, SwitchQuantityView


def as_view(q: ParamQuantityView | ParamQuantity) -> ParamQuantityView:
    # Built-in configuration only: a class factory would return its own object.
    assert isinstance(q, ParamQuantityView)
    return q


def builtin_views(m: Module) -> list[ParamQuantityView]:
    return [as_view(q) for q in m.param_quantities]


def configure(m: Module, base: float = 0) -> None:
    m.config(3, 0, 0)
    m.config_param(0, 1, 8, 2, "Pitch", " Hz", base, 3, 4)
    m.config_switch(1, -1, 1, 0, "Mode", ["Low", "Middle", "High"])
    m.config_button(2, "Trigger")


def test_quantity_defaults_metadata_and_identity() -> None:
    def defaults(m: Module) -> None:
        m.config(2, 0, 0)
        for i, q in enumerate(m.param_quantities):
            assert q.param_id == i and q.module is m
            assert q.get_param() is m.params[i]
            assert q.get_label() == f"#{i + 1}"
            assert (q.min_value, q.max_value, q.default_value) == (0, 1, 0)
            assert (q.name, q.unit, q.description) == ("", "", "")
            assert (q.display_base, q.display_multiplier, q.display_offset, q.display_precision) == (0, 1, 0, 5)
            assert (q.reset_enabled, q.randomize_enabled, q.smooth_enabled, q.snap_enabled) == (
                True,
                True,
                False,
                False,
            )

    m = Module.create(defaults)
    assert len(m.param_quantities) == 2 and m.param_quantities[-1] is m.get_param_quantity(1)
    m.close()
    m = Module.create(configure)
    try:
        assert [p.value for p in m.params] == [2, 0, 0]
        for i, q in enumerate(m.param_quantities):
            assert q is m.get_param_quantity(i) and q.module is m and q.get_param() is m.params[i]
            assert q.get_label() == ["Pitch", "Mode", "Trigger"][i]
        q = m.param_quantities[0]
        assert q.get_min_value() == q.min_value == 1
        assert q.get_max_value() == q.max_value == 8
        assert q.get_default_value() == q.default_value == 2
        q.description = "Pitch description"
        q.display_precision = 7
        assert q.get_description() == "Pitch description" and q.get_display_precision() == 7
        assert q.get_unit() == " Hz"
        for name, value in (
            ("min_value", 0.5),
            ("max_value", 10.0),
            ("default_value", 3.0),
            ("name", "New pitch"),
            ("unit", " V"),
            ("display_base", 2.0),
            ("display_multiplier", 4.0),
            ("display_offset", 1.0),
            ("reset_enabled", False),
            ("randomize_enabled", False),
            ("smooth_enabled", True),
            ("snap_enabled", True),
        ):
            setattr(q, name, value)
            assert getattr(q, name) == value
        for i in (1, 2):
            switch = m.param_quantities[i]
            assert isinstance(switch, SwitchQuantityView)
            assert (switch.snap_enabled, switch.smooth_enabled, switch.randomize_enabled) == (True, False, i == 1)
            expected = ["Low", "Middle", "High"] if i == 1 else []
            assert switch.labels.tolist() == expected
            copy = switch.labels.tolist()
            copy.append("Not written through")
            assert switch.labels.tolist() == expected
            switch.labels = ["a", "b"]
            assert switch.labels.tolist() == ["a", "b"]
    finally:
        m.close()


def value_actions(q: ParamQuantityView) -> list[Callable[[], Any]]:
    return [
        q.get_value,
        lambda: q.set_value(0.5),
        q.get_immediate_value,
        lambda: q.set_immediate_value(0.5),
        q.get_display_value,
        lambda: q.set_display_value(0.5),
        q.get_display_value_string,
        lambda: q.set_display_value_string("0.5"),
        q.reset,
        q.randomize,
        q.get_string,
        q.is_min,
        q.is_max,
        q.set_min,
        q.set_max,
        q.toggle,
        lambda: q.move_value(0.25),
        lambda: q.set_scaled_value(0.5),
        q.get_scaled_value,
        lambda: q.move_scaled_value(0.25),
    ]


def test_native_value_display_and_switch_reference_or_missing_context() -> None:
    if _rackpy_host.bound_context_generation() is None:
        m = Module.create(configure)
        try:
            for q in builtin_views(m):
                for action in value_actions(q):
                    with pytest.raises(ContextError, match="live bound"):
                        action()
        finally:
            m.close()
        return

    from _rackpy_host import _testing

    for case in _testing.quantity_reference():

        def build(m: Module, base: float = case["base"]) -> None:
            configure(m, base)

        m = Module.create(build)
        try:
            q = as_view(m.param_quantities[0])
            assert [p.value for p in m.params] == case["defaults"]
            for index, key in ((1, "switch_flags"), (2, "button_flags")):
                quantity = m.param_quantities[index]
                assert [quantity.snap_enabled, quantity.smooth_enabled, quantity.randomize_enabled] == case[key]
            for x, value, display, text in case["values"]:
                q.set_value(x)
                assert q.get_value() == q.get_immediate_value() == pytest.approx(value)
                assert q.get_display_value() == pytest.approx(display)
                assert q.get_display_value_string() == text
            for display, expected in case["displays"]:
                q.set_display_value(display)
                assert q.get_immediate_value() == pytest.approx(expected)
            q.set_display_value_string("2 + 8")
            assert q.get_immediate_value() == pytest.approx(case["parsed"])
            before = q.get_immediate_value()
            q.set_display_value_string("not an expression ??")
            assert q.get_immediate_value() == before
            q.reset()
            assert q.get_value() == case["reset"]
            q.display_multiplier = 0
            q.set_display_value(50)
            assert q.get_value() == 1  # Rack's zero-multiplier rule + clamp
            q.display_multiplier = 3
            q.set_display_value(float("nan"))
            assert q.get_value() == 1  # invalid inverse does not change it
            for _ in range(100):
                q.randomize()
                assert q.min_value <= q.get_value() <= q.max_value
            switch = m.param_quantities[1]
            assert isinstance(switch, SwitchQuantityView)
            for x, value, text in case["switches"]:
                switch.set_immediate_value(x)
                assert switch.get_value() == value and switch.get_display_value_string() == text
            switch.set_display_value_string("mIDdle")
            assert switch.get_value() == case["switch_parsed"]
            switch.set_display_value_string("missing label")
            assert switch.get_value() == case["switch_parsed"]
            m.params[1].value = 1e30
            for stringify in (switch.get_display_value_string, switch.get_string):
                with pytest.raises(ValueError, match="label index"):
                    stringify()
            switch.set_immediate_value(0)
            for _ in range(100):
                switch.randomize()
                assert switch.get_value() in (-1, 0, 1)
            # Mixed value/range implementations are native. No callback into Python.
            q.smooth_enabled = True
            with pytest.raises(OwnershipError, match="engine-owned"):
                q.set_value(3)
            q.set_immediate_value(3)
            assert q.get_value() == q.get_immediate_value() == 3
            q.smooth_enabled = False
            with pytest.warns(DeprecationWarning):
                q.set_smooth_value(4)
            with pytest.warns(DeprecationWarning):
                assert q.get_smooth_value() == 4
            # An explicitly foreign context is refused before Rack access.
            with pytest.raises(ContextError):
                _testing.with_foreign_context(q.get_value)
        finally:
            m.close()


def test_quantity_validation_and_replacement_rollback() -> None:
    def build(m: Module) -> None:
        with pytest.raises(IndexError):
            m.config_param(0, 0, 1, 0)
        m.config(2, 0, 0)
        old = as_view(m.get_param_quantity(0))
        for bounds in (
            (1, 1, 1),
            (2, 1, 1),
            (0, 1, -1),
            (0, 1, 2),
            (math.nan, 1, 0),
            (0, math.inf, 0),
            (0, 1, math.nan),
        ):
            with pytest.raises(ValueError):
                m.config_param(0, *bounds)
            assert old.alive and old is m.param_quantities[0] and m.params[0].value == 0
        for coefficients in ((1, 1, 0), (-1, 1, 0), (0, math.inf, 0), (0, 1, math.nan)):
            with pytest.raises(ValueError):
                m.config_param(
                    0,
                    0,
                    1,
                    0,
                    display_base=coefficients[0],
                    display_multiplier=coefficients[1],
                    display_offset=coefficients[2],
                )
        for i in (-1, 2):
            for method in (
                lambda i: m.config_param(i, 0, 1, 0),
                lambda i: m.config_switch(i, 0, 1, 0),
                m.config_button,
            ):
                with pytest.raises(IndexError):
                    method(i)
        with pytest.raises(TypeError):
            m.config_switch(0, 0, 1, 0, labels=[object()])  # type: ignore[list-item]
        assert old.alive and old is m.param_quantities[0]
        # Template/class arguments are not silently accepted and ignored.
        with pytest.raises(TypeError):
            m.config_param(0, 0, 1, 0, quantity_class=object)  # type: ignore[call-overload]
        new = m.config_button(0)
        assert_stale(old)
        assert new is m.param_quantities[0]
        assert m.params[0].value == 0

    m = Module.create(build)
    late_actions: list[Callable[[], Any]] = [
        lambda: m.config_param(0, 0, 1, 0),
        lambda: m.config_switch(0, 0, 1, 0),
        lambda: m.config_button(0),
    ]
    for action in late_actions:
        with pytest.raises(OwnershipError):
            action()
    for i in (-1, 2):
        with pytest.raises(IndexError):
            m.get_param_quantity(i)
    for i in (-3, 2):
        with pytest.raises(IndexError):
            _ = m.param_quantities[i]
    q = m.param_quantities[0]
    for name, value in (("module", None), ("param_id", -1)):
        with pytest.raises(AttributeError):
            setattr(q, name, value)
    with pytest.raises(TypeError):
        m.param_quantities[0] = q  # type: ignore[index]
    m.close()


def accesses(q: ParamQuantityView) -> list[Callable[[], Any]]:
    actions = [
        *value_actions(q),
        lambda: q.module,
        lambda: q.param_id,
        q.get_param,
        q.get_min_value,
        q.get_max_value,
        q.get_default_value,
        q.get_label,
        q.get_unit,
        q.get_description,
        q.get_display_precision,
        q.get_range,
        q.is_bounded,
        lambda: q.to_scaled(0.5),
        lambda: q.from_scaled(0.5),
        q.get_smooth_value,
        lambda: q.set_smooth_value(0),
    ]
    for field in (
        "min_value",
        "max_value",
        "default_value",
        "name",
        "unit",
        "display_base",
        "display_multiplier",
        "display_offset",
        "display_precision",
        "description",
        "reset_enabled",
        "randomize_enabled",
        "smooth_enabled",
        "snap_enabled",
    ):

        def read(field: str = field) -> Any:
            return getattr(q, field)

        def write(field: str = field) -> None:
            setattr(q, field, "" if field in ("name", "unit", "description") else 0)

        actions.extend((read, write))
    if isinstance(q, SwitchQuantityView):
        actions.extend((lambda: q.labels, lambda: setattr(q, "labels", [])))
    return actions


def assert_stale(q: ParamQuantityView) -> None:
    assert not q.alive
    for action in accesses(q):
        with pytest.raises(StaleHandleError):
            action()


def test_quantity_slot_replacement_churn_and_owner_collection() -> None:
    retained: list[ParamQuantityView] = []

    def build(m: Module) -> None:
        m.config(2, 0, 0)
        untouched = as_view(m.param_quantities[1])
        param = m.params[0]
        old = as_view(m.param_quantities[0])
        for i in range(1000):
            new = m.config_button(0) if i % 2 else m.config_param(0, 0, 1, 0.25)
            assert_stale(old)
            assert untouched.alive and untouched is m.param_quantities[1]
            assert new.get_param() is param and param.alive
            old = new
        retained.extend((old, untouched))

    m = Module.create(build)
    m.close()
    for q in retained:
        assert_stale(q)
    seen: weakref.WeakSet[Module] = weakref.WeakSet()
    for _ in range(1000):
        m = Module.create(lambda m: m.config(1, 0, 0))
        seen.add(m)
        q, group = as_view(m.param_quantities[0]), m.param_quantities
        del m
        assert_stale(q)
        with pytest.raises(StaleHandleError):
            len(group)
    gc.collect()
    assert not seen

    def fail(m: Module) -> None:
        configure(m)
        retained.append(as_view(m.param_quantities[0]))
        raise RuntimeError("after quantities")

    with pytest.raises(RuntimeError, match="after quantities"):
        Module.create(fail)
    assert_stale(retained[-1])


def test_quantity_wrong_thread() -> None:
    m = Module.create(configure)
    actions: list[Callable[[], Any]] = [
        lambda: m.param_quantities,
        lambda: len(m.param_quantities),
        lambda: m.get_param_quantity(0),
        lambda: m.config_button(0),
        lambda: m.config_param(0, 0, 1, 0),
        lambda: m.config_switch(0, 0, 1, 0),
    ]
    for q in builtin_views(m):
        actions.extend(accesses(q))
    errors: list[BaseException] = []

    def worker() -> None:
        for action in actions:
            try:
                action()
            except BaseException as e:
                errors.append(e)

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    assert len(errors) == len(actions) and all(isinstance(e, ThreadError) for e in errors)
    m.close()
