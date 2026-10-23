# SPDX-License-Identifier: GPL-3.0-or-later
"""Unattached SDK quantities: owned storage, native null semantics and UI virtuals."""

import gc
import math
import threading
import weakref
from collections.abc import Callable
from typing import Any

import pytest

from rackpy.errors import StaleHandleError, ThreadError
from rackpy.raw import Quantity
from rackpy.raw.engine import Module, ParamQuantity, SwitchQuantity


@pytest.mark.parametrize("quantity_class", [ParamQuantity, SwitchQuantity])
def test_unattached_native_defaults_and_noops(quantity_class: type[ParamQuantity]) -> None:
    q = quantity_class()
    assert isinstance(q, Quantity)
    assert q.module is None and q.get_param() is None and q.param_id == -1
    assert [q.min_value, q.max_value, q.default_value] == [0, 1, 0]
    assert q.display_precision == 5 and q.display_multiplier == 1
    assert q.reset_enabled and q.randomize_enabled and not q.smooth_enabled and not q.snap_enabled
    for smooth in (False, True):
        q.smooth_enabled = smooth
        for value in (-100.0, 0.5, math.inf, math.nan):
            q.set_value(value)
            q.set_immediate_value(value)
            q.set_display_value(value)
            q.set_display_value_string("1 + 2")
            q.reset()
            q.randomize()
            q.set_min()
            q.set_max()
            q.toggle()
            q.move_value(value)
            q.set_scaled_value(value)
            q.move_scaled_value(value)
            assert q.get_value() == q.get_immediate_value() == 0
        with pytest.warns(DeprecationWarning):
            q.set_smooth_value(0.5)
        with pytest.warns(DeprecationWarning):
            assert q.get_smooth_value() == 0
    assert q.is_min() and not q.is_max() and q.get_scaled_value() == 0
    owner = Module.create()
    try:
        q.module = owner
        assert q.module is owner and q.get_param() is None
        q.module = None
    finally:
        owner.close()


def test_standalone_native_reference(reference: dict[str, Any]) -> None:
    for case in reference["standalone_quantities"]:
        q = ParamQuantity()
        q.min_value, q.max_value, q.default_value = -2, 3, 1
        q.display_base, q.display_multiplier, q.display_offset = case["base"], 3, 4
        q.param_id, q.unit, q.description = 2, " V", "Description"
        q.set_value(2)
        q.set_immediate_value(1)
        q.set_display_value(7)
        q.set_display_value_string("2 + 8")
        q.reset()
        q.randomize()
        assert [q.get_value(), q.get_immediate_value(), q.get_display_value()] == case["values"]
        assert [q.get_label(), q.get_unit(), q.get_description()] == case["text"]
        assert (q.module is None and q.get_param() is None) == case["null"]
        assert q.is_bounded() == case["bounded"]
    q.min_value, q.max_value = -math.inf, math.inf
    assert not q.is_bounded() and q.to_scaled(3) == q.from_scaled(3) == 3
    q.min_value = q.max_value = 5
    assert q.get_range() == 0 and q.to_scaled(5) == 0
    q.display_base = -2
    assert math.isinf(q.get_display_value())
    q.param_id = 2**31 - 1
    with pytest.raises(ValueError, match="overflows"):
        q.get_label()
    q.name = "Explicit"
    assert q.get_label() == "Explicit"
    q.param_id = -(2**31)
    q.name = ""
    assert q.get_label() == f"#{-(2**31) + 1}"


@pytest.mark.parametrize("quantity_class", [ParamQuantity, SwitchQuantity])
def test_ui_virtuals_base_calls_and_errors(quantity_class: type[ParamQuantity]) -> None:
    # Call through bound base descriptors, not merely Python method lookup.
    for name, result, args in [
        ("get_display_value", 7.0, ()),
        ("set_display_value", None, (4.0,)),
        ("get_display_precision", 2, ()),
        ("get_display_value_string", "Pretty", ()),
        ("set_display_value_string", None, ("Input",)),
        ("get_label", "Custom", ()),
        ("get_unit", " Hz", ()),
        ("get_string", "Complete", ()),
        ("reset", None, ()),
        ("randomize", None, ()),
        ("get_description", "Custom description", ()),
    ]:
        calls: list[tuple[object, ...]] = []

        def answer(
            self: object, *values: object, calls: list[tuple[object, ...]] = calls, result: object = result
        ) -> object:
            calls.append(values)
            return result

        cls = type("Custom", (quantity_class,), {name: answer})
        q = cls()
        base = ParamQuantity if name == "get_description" else Quantity
        assert getattr(base, name)(q, *args) == result
        assert calls == [args]

        def fail(self: object, *values: object) -> object:
            raise ValueError("callback failure")

        cls = type("Failing", (quantity_class,), {name: fail})
        with pytest.raises(ValueError, match="callback failure"):
            getattr(base, name)(cls(), *args)

    class Labeled(quantity_class):  # type: ignore[misc, valid-type]
        def get_label(self) -> str:
            return super().get_label() + " suffix"  # type: ignore[no-any-return]

    assert Quantity.get_label(Labeled()) == "#0 suffix"

    class Formatted(quantity_class):  # type: ignore[misc, valid-type]
        def get_label(self) -> str:
            return "Safe label"

        def get_display_value_string(self) -> str:
            return "Safe display"

    formatted = Formatted()
    formatted.param_id, formatted.min_value = 2**31 - 1, math.nan
    # Native overflow guards protect the SDK fallback, not Python overrides
    # that deliberately handle raw/unbounded metadata themselves.
    assert Quantity.get_label(formatted) == "Safe label"
    assert Quantity.get_display_value_string(formatted) == "Safe display"
    for name in ("get_value", "set_value", "get_min_value", "get_max_value", "get_default_value"):
        with pytest.raises(TypeError, match="engine thread"):
            type("Forbidden", (quantity_class,), {name: lambda self: 1.0})


def test_owned_switch_labels_and_gc() -> None:
    for _ in range(1000):
        q = SwitchQuantity()
        alias = q.labels
        assert alias is q.labels
        q.labels = ["Zero", "One", "Two"]
        alias[0] = "Bass"
        del alias[-1]
        alias.insert(1, "Center")
        alias.append("Extra")
        alias.extend(["More"])
        assert alias.tolist() == ["Bass", "Center", "One", "Extra", "More"]
        assert q.get_display_value_string() == "Bass"
        q.set_display_value_string("oNE")  # unattached: native no-op
        assert q.get_value() == 0
        snapshot = iter(alias)
        q.labels = ["Replaced"]
        assert list(snapshot) == ["Bass", "Center", "One", "Extra", "More"]
        with pytest.raises(TypeError):
            q.labels = ["Partial", object()]  # type: ignore[list-item]
        assert alias.tolist() == ["Replaced"]
        with pytest.raises(TypeError):
            alias.extend(["Partial", object()])  # type: ignore[list-item]
        assert alias.tolist() == ["Replaced"]
        ref = weakref.ref(q)
        del q
        assert ref() is not None  # owned alias keeps storage, unlike borrowed aliases
        alias.clear()
        assert not alias.tolist()
        del alias
        assert ref() is None
    gc.collect()
    q = SwitchQuantity()
    for lower in (math.nan, math.inf, -(2**31 + 1.0)):
        q.min_value = lower
        with pytest.raises(ValueError, match="label index"):
            q.get_display_value_string()


def test_owned_alias_cycles_and_tampering_are_safe() -> None:
    class Cyclic(SwitchQuantity):
        retained: object

    refs = []
    for _ in range(1000):
        q = Cyclic()
        q.retained = q.labels
        refs.append(weakref.ref(q))
        del q
    gc.collect()
    assert all(ref() is None for ref in refs)

    # The private ownership edge is GC-visible, but removing it cannot leave
    # the resolver pointing into freed native storage.
    quantity = SwitchQuantity()
    alias = quantity.labels
    delattr(alias, "_rackpy_owner")
    del quantity
    with pytest.raises(StaleHandleError):
        alias.tolist()


@pytest.mark.parametrize("quantity_class", [ParamQuantity, SwitchQuantity])
def test_standalone_thread_guards(quantity_class: type[ParamQuantity]) -> None:
    q = quantity_class()
    actions: list[Callable[[], object]] = [
        quantity_class,
        lambda: q.module,
        q.get_param,
        q.get_immediate_value,
        lambda: q.set_immediate_value(1),
        q.get_description,
        lambda: q.name,
        lambda: setattr(q, "name", "Unsafe"),
        lambda: q.param_id,
        lambda: setattr(q, "param_id", 1),
    ]
    for name in (
        "get_value",
        "get_min_value",
        "get_max_value",
        "get_default_value",
        "get_display_value",
        "get_display_precision",
        "get_display_value_string",
        "get_label",
        "get_unit",
        "get_string",
        "reset",
        "randomize",
        "get_range",
        "is_bounded",
        "get_scaled_value",
        "is_min",
        "is_max",
        "set_min",
        "set_max",
        "toggle",
    ):
        actions.append(getattr(q, name))
    for name in (
        "set_value",
        "set_display_value",
        "move_value",
        "set_scaled_value",
        "move_scaled_value",
        "to_scaled",
        "from_scaled",
    ):

        def invoke(name: str = name) -> object:
            return getattr(q, name)(0.5)

        actions.append(invoke)
    actions.append(lambda: q.set_display_value_string("0"))
    if isinstance(q, SwitchQuantity):
        alias = q.labels
        actions += [
            lambda: q.labels,
            lambda: len(alias),
            lambda: alias.append("Unsafe"),
            lambda: setattr(q, "labels", []),
        ]
    outcomes: list[BaseException] = []

    def worker() -> None:
        for action in actions:
            try:
                action()
            except BaseException as error:
                outcomes.append(error)

    thread = threading.Thread(target=worker)
    thread.start()
    thread.join()
    assert len(outcomes) == len(actions) and all(isinstance(e, ThreadError) for e in outcomes)
    assert q.name == "" and q.param_id == -1
