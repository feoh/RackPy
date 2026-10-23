# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14B2b6: quantity class factories transfer Python quantities into module slots."""

import gc
import threading
import weakref
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest

from rackpy.errors import ContextError, OwnershipError, StaleHandleError, ThreadError
from rackpy.raw import Quantity
from rackpy.raw.engine import Module, ParamQuantity, ParamQuantityView, PortInfo, SwitchQuantity, SwitchQuantityView

HOOKS = getattr(_rackpy_host, "_testing", None)
LIVE = _rackpy_host.bound_context_generation() is not None


class CustomQuantity(ParamQuantity):
    def __init__(self) -> None:
        super().__init__()
        self.description = "From constructor"
        self.randomize_enabled = False
        self.display_precision = 3

    def get_label(self) -> str:
        return "Custom " + self.name


class CustomSwitch(SwitchQuantity):
    def __init__(self) -> None:
        super().__init__()
        self.description = "From constructor"
        self.randomize_enabled = False
        self.display_precision = 3
        self.labels = ["Off", "On"]

    def get_label(self) -> str:
        return "Custom " + self.name


def build(configure: Callable[[Module], object], params: int = 4) -> Module:
    def run(m: Module) -> None:
        m.config(params, 0, 0, 0)
        configure(m)

    return Module.create(run)


def record(m: Module, q: ParamQuantity, i: int) -> list[object]:
    labels = list(q.labels) if isinstance(q, SwitchQuantity) else []
    return [
        q.get_label(),
        q.description,
        q.name,
        q.unit,
        q.param_id,
        q.min_value,
        q.max_value,
        q.default_value,
        q.display_base,
        q.display_multiplier,
        q.display_offset,
        q.display_precision,
        q.snap_enabled,
        q.smooth_enabled,
        q.randomize_enabled,
        q.module is m,
        m.params[i].value,
        labels,
    ]


def bound_actions(q: ParamQuantity) -> list[Callable[[], object]]:
    actions: list[Callable[[], object]] = [
        lambda: q.module,
        lambda: setattr(q, "module", None),
        lambda: q.param_id,
        lambda: setattr(q, "name", "x"),
        lambda: q.min_value,
        q.get_label,
        q.get_description,
        q.get_min_value,
        q.get_default_value,
        q.get_value,
        q.get_display_value_string,
        q.get_param,
        q.get_immediate_value,
        q.is_bounded,
    ]
    if isinstance(q, SwitchQuantity):
        switch = q
        actions += [lambda: switch.labels, lambda: setattr(switch, "labels", ["x"])]
    return actions


def assert_stale(q: ParamQuantity) -> None:
    assert not q.alive
    for i, action in enumerate(bound_actions(q)):
        with pytest.raises(StaleHandleError, match="replaced or its module was deleted"):
            action()
            pytest.fail(f"bound action {i} did not raise")


def test_factories_match_native_reference(reference: dict[str, Any]) -> None:
    made: list[ParamQuantity] = []

    def configure(m: Module) -> None:
        made.append(m.config_param(0, -1, 1, 0.5, "Gain", " dB", 0, 2, 3, quantity_type=CustomQuantity))
        made.append(m.config_switch(1, 0, 2, 1, "Mode", ["X", "Y", "Z"], quantity_type=CustomSwitch))
        made.append(m.config_button(2, "Push", quantity_type=CustomSwitch))
        made.append(m.config_param(3, 0, 4, 2, "Steps", quantity_type=CustomSwitch))

    baseline = _rackpy_host.transferred_objects()
    m = build(configure)
    try:
        assert [record(m, q, i) for i, q in enumerate(made)] == reference["quantity_factories"]
        assert [type(q) for q in made] == [CustomQuantity, CustomSwitch, CustomSwitch, CustomSwitch]
        for i, q in enumerate(made):
            assert q is m.get_param_quantity(i) is m.param_quantities[i] and q.alive
        assert list(m.param_quantities) == made
        # The SDK's get_param identity is the module's own Param view.
        assert made[0].get_param() is m.params[0]
        assert _rackpy_host.transferred_objects() == baseline + 4
    finally:
        m.close()
    assert _rackpy_host.transferred_objects() == baseline


def test_replacement_retires_and_matches_native_reference(reference: dict[str, Any]) -> None:
    observed: list[object] = []
    finalized: list[str] = []

    def configure(m: Module) -> None:
        first = m.config_param(0, -1, 1, 0.5, "Gain", quantity_type=CustomQuantity)
        weakref.finalize(first, finalized.append, "first")
        again = m.config_param(0, 0, 1, 0.25, "Again", quantity_type=CustomQuantity)
        observed.extend([[first.alive, again.alive].count(False), m.params[0].value])
        assert m.get_param_quantity(0) is again
        assert_stale(first)
        del first
        gc.collect()
        assert finalized == ["first"]
        plain = m.config_param(0, 0, 1, 0.75, "Plain")
        assert type(plain) is ParamQuantityView and not again.alive
        observed.extend([2, plain.get_label(), m.params[0].value])

    m = build(configure)
    try:
        assert observed == reference["quantity_replacement"]
    finally:
        m.close()


def test_close_gc_worker_and_rollback_retire() -> None:
    held: list[ParamQuantity] = []

    def configure(m: Module) -> None:
        held.append(m.config_param(0, 0, 1, 0, "P", quantity_type=CustomQuantity))
        switch = m.config_switch(1, 0, 1, 0, "S", ["A", "B"], quantity_type=CustomSwitch)
        held.append(switch)
        alias = switch.labels
        assert alias is switch.labels and alias.tolist() == ["A", "B"]
        aliases.append(alias)

    aliases: list[Any] = []
    baseline = _rackpy_host.transferred_objects()
    m = build(configure)
    m.close()
    for q in held:
        assert_stale(q)
    with pytest.raises(StaleHandleError):
        len(aliases[0])
    assert _rackpy_host.transferred_objects() == baseline

    held.clear()
    m = build(configure)
    owner = weakref.ref(m)
    del m
    gc.collect()
    assert owner() is None and len(held) == 2 and not any(q.alive for q in held)

    held.clear()
    pending = [build(configure)]
    thread = threading.Thread(target=pending.clear)
    thread.start()
    thread.join()
    assert len(held) == 2 and not any(q.alive for q in held)

    held.clear()

    def failing(m: Module) -> None:
        configure(m)
        raise ValueError("rollback transferred quantities")

    with pytest.raises(ValueError, match="rollback transferred"):
        build(failing)
    assert len(held) == 2 and not any(q.alive for q in held)
    assert _rackpy_host.transferred_objects() == baseline


def test_class_constructor_and_bounds_errors_leave_slot_unchanged() -> None:
    constructed: list[str] = []

    class Counting(ParamQuantity):
        def __init__(self) -> None:
            super().__init__()
            constructed.append("counting")

    class Failing(ParamQuantity):
        def __init__(self) -> None:
            super().__init__()
            raise RuntimeError("constructor failed")

    class Foreign(ParamQuantity):
        def __new__(cls) -> Foreign:
            return CustomQuantity()  # type: ignore[return-value]

    def configure(m: Module) -> None:
        view = m.get_param_quantity(0)
        bad: list[Any] = [object, ParamQuantityView, PortInfo, Quantity, CustomQuantity(), "ParamQuantity"]
        for wrong in bad:
            with pytest.raises(TypeError, match="quantity_type must be ParamQuantity"):
                m.config_param(0, 0, 1, 0, quantity_type=wrong)
        for wrong in (ParamQuantity, CustomQuantity, SwitchQuantityView):
            with pytest.raises(TypeError, match="quantity_type must be SwitchQuantity"):
                m.config_switch(0, 0, 1, 0, quantity_type=wrong)  # type: ignore[type-var]
            with pytest.raises(TypeError, match="quantity_type must be SwitchQuantity"):
                m.config_button(0, quantity_type=wrong)  # type: ignore[type-var]
        with pytest.raises(RuntimeError, match="constructor failed"):
            m.config_param(0, 0, 1, 0, quantity_type=Failing)
        with pytest.raises(TypeError, match="exactly quantity_type"):
            m.config_param(0, 0, 1, 0, quantity_type=Foreign)
        # Rack's arguments are validated before the class is constructed.
        for args in ((1, 1, 1), (0, 1, 2), (float("nan"), 1, 0)):
            with pytest.raises(ValueError):
                m.config_param(0, *args, quantity_type=Counting)
        for i in (-1, 4):
            with pytest.raises(IndexError):
                m.config_param(i, 0, 1, 0, quantity_type=Counting)
        assert constructed == []
        assert m.get_param_quantity(0) is view and view.alive

    baseline = _rackpy_host.transferred_objects()
    m = build(configure)
    m.close()
    assert _rackpy_host.transferred_objects() == baseline


def test_reuse_reentry_and_phase_errors() -> None:
    seen: list[ParamQuantity] = []
    module: list[Module] = []

    class Reused(ParamQuantity):
        def __new__(cls) -> Reused:
            if seen:
                return seen[0]  # type: ignore[return-value]
            return super().__new__(cls)

    inner: list[ParamQuantity] = []

    class Reentrant(ParamQuantity):
        def __init__(self) -> None:
            super().__init__()
            if not inner:
                inner.append(module[0].config_param(1, 0, 1, 0, "Inner", quantity_type=CustomQuantity))

    def configure(m: Module) -> None:
        module.append(m)
        seen.append(m.config_param(0, 0, 1, 0, quantity_type=Reused))
        with pytest.raises(OwnershipError, match="already owns"):
            m.config_param(2, 0, 1, 0, quantity_type=Reused)
        m.config_param(0, 0, 1, 0)
        assert not seen[0].alive
        with pytest.raises(StaleHandleError, match="already released"):
            m.config_param(2, 0, 1, 0, quantity_type=Reused)
        outer = m.config_param(1, 0, 1, 0.5, "Outer", quantity_type=Reentrant)
        assert not inner[0].alive and m.get_param_quantity(1) is outer and m.params[1].value == 0.5

    m = build(configure)
    try:
        with pytest.raises(OwnershipError, match="constructor callback"):
            m.config_param(0, 0, 1, 0, quantity_type=CustomQuantity)
    finally:
        m.close()


def test_slot_quantities_main_thread_rule() -> None:
    m = build(
        lambda m: (
            m.config_param(0, 0, 1, 0, quantity_type=CustomQuantity),
            m.config_switch(1, 0, 1, 0, "S", ["A", "B"], quantity_type=CustomSwitch),
        )
    )
    quantities = [m.get_param_quantity(0), m.get_param_quantity(1)]
    outcomes: list[BaseException] = []
    alive: list[bool] = []

    def worker() -> None:
        for q in quantities:
            assert isinstance(q, ParamQuantity)
            alive.append(q.alive)
            for action in bound_actions(q):
                try:
                    action()
                except BaseException as error:
                    outcomes.append(error)

    try:
        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        expected = sum(len(bound_actions(q)) for q in quantities if isinstance(q, ParamQuantity))
        assert alive == [True, True]
        assert len(outcomes) == expected and all(isinstance(e, ThreadError) for e in outcomes)
    finally:
        m.close()


def test_values_need_live_context_and_refuse_unregistered_smoothing() -> None:
    def configure(m: Module) -> None:
        m.config_param(0, 0, 10, 2, "Level", quantity_type=CustomQuantity)
        m.config_switch(1, 0, 2, 1, "Mode", ["Low", "Mid", "High"], quantity_type=CustomSwitch)

    m = build(configure, params=2)
    q, switch = m.get_param_quantity(0), m.get_param_quantity(1)
    assert isinstance(q, CustomQuantity) and isinstance(switch, CustomSwitch)
    try:
        # Metadata needs no Rack context, only the main thread.
        assert q.get_label() == "Custom Level" and q.get_min_value() == 0 and q.get_default_value() == 2
        if not LIVE:
            for action in (q.get_value, lambda: q.set_value(1), q.get_display_value_string, q.reset):
                with pytest.raises(ContextError, match="live bound"):
                    action()
            return
        assert q.get_value() == m.params[0].value == 2
        q.set_value(4)
        assert q.get_value() == m.params[0].value == 4
        assert q.get_string() == "Custom Level: " + q.get_display_value_string()
        assert switch.get_display_value_string() == "Mid"
        switch.labels[1] = "Centre"
        assert switch.get_display_value_string() == "Centre"
        q.reset()
        assert q.get_value() == 2
        q.smooth_enabled = True
        with pytest.raises(OwnershipError, match="engine-owned"):
            q.set_value(3)
        q.set_immediate_value(3)
        assert m.params[0].value == 3
        if HOOKS is not None:
            with pytest.raises(ContextError):
                HOOKS.with_foreign_context(q.get_value)
    finally:
        m.close()


def test_rack_callers_get_native_mixed_and_contained_ui_overrides() -> None:
    if not LIVE or HOOKS is None:
        return  # the production startup suite has neither the hooks nor a live engine
    errors: list[str] = []

    class Erroring(ParamQuantity):
        def get_label(self) -> str:
            errors.append("label")
            raise ValueError("label failed")

    def configure(m: Module) -> None:
        m.config_param(0, 0, 10, 2.5, "Level", quantity_type=CustomQuantity)
        m.config_param(1, 0, 10, 5, "Broken", quantity_type=Erroring)

    m = build(configure, params=2)
    try:
        q, broken = m.get_param_quantity(0), m.get_param_quantity(1)
        before = _rackpy_host.dispatch_stats()
        # Mixed methods on a Rack thread: native, no Python, no refusal, no throw.
        assert HOOKS.read_slot_quantity_as_engine(m, 0, 10000) == pytest.approx(0.25)
        assert _rackpy_host.dispatch_stats() == before
        # UI virtuals Rack calls on the main thread without the GIL use the override...
        assert isinstance(q, ParamQuantity) and isinstance(broken, ParamQuantity)
        assert HOOKS.quantity_string_as_rack(q) == q.get_string() == "Custom Level: 2.5"
        # ...and a raising override falls back to Rack's own answer.
        assert HOOKS.quantity_string_as_rack(broken) == "Broken: " + broken.get_display_value_string()
        assert errors == ["label"]
    finally:
        m.close()


def test_override_closing_its_module_leaves_a_stale_object() -> None:
    owner: list[Module] = []

    class Closing(ParamQuantity):
        def get_description(self) -> str:
            owner[0].close()
            assert not self.alive
            return super().get_description()

    m = build(lambda m: m.config_param(0, 0, 1, 0, quantity_type=Closing), params=1)
    owner.append(m)
    q = m.get_param_quantity(0)
    assert isinstance(q, Closing)
    with pytest.raises(StaleHandleError):
        q.get_description()
    assert not m.alive and not q.alive


def test_one_thousand_replacements_and_owner_churns() -> None:
    baseline = _rackpy_host.transferred_objects()
    tracked: weakref.WeakSet[ParamQuantity] = weakref.WeakSet()
    owners: weakref.WeakSet[Module] = weakref.WeakSet()

    def configure(m: Module) -> None:
        for i in range(1000):
            tracked.add(m.config_switch(0, 0, 1, 0, str(i), ["A", "B"], quantity_type=CustomSwitch))
        assert _rackpy_host.transferred_objects() == baseline + 1

    m = build(configure, params=1)
    gc.collect()
    assert len(tracked) == 1 and next(iter(tracked)) is m.get_param_quantity(0)
    m.close()

    for i in range(1000):
        module = build(lambda m: m.config_button(0, "B", quantity_type=CustomSwitch), params=1)
        owners.add(module)
        q = module.get_param_quantity(0)
        assert isinstance(q, CustomSwitch)
        if i % 2:
            module.close()
        del module
        assert not q.alive
    gc.collect()
    assert not owners and not tracked
    assert _rackpy_host.transferred_objects() == baseline


def test_builtin_views_and_standalone_rules_are_unchanged() -> None:
    m = build(lambda m: (m.config_param(0, 0, 1, 0), m.config_switch(1, 0, 1, 0)), params=2)
    try:
        assert type(m.get_param_quantity(0)) is ParamQuantityView
        assert type(m.get_param_quantity(1)) is SwitchQuantityView
        standalone = CustomQuantity()
        assert standalone.alive and standalone.module is None
    finally:
        m.close()
