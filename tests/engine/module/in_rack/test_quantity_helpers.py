# SPDX-License-Identifier: GPL-3.0-or-later
"""Borrowed native Quantity helpers and mutable, lifetime-checked switch labels."""

import threading
from collections.abc import Callable, Iterator
from functools import partial
from typing import Any

import _rackpy_host
import pytest

from rackpy.errors import ContextError, OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import Module, SwitchLabels, SwitchQuantityView


def build(m: Module) -> None:
    m.config(1, 0, 0)
    m.config_switch(0, -1, 1, 0, "Mode", ["Low", "Middle", "High"])


def switch(m: Module) -> SwitchQuantityView:
    q = m.param_quantities[0]
    assert isinstance(q, SwitchQuantityView)
    return q


def label_actions(labels: SwitchLabels) -> list[Callable[[], Any]]:
    return [
        lambda: len(labels),
        lambda: labels[0],
        lambda: labels.__setitem__(0, "Replaced"),
        lambda: labels.__delitem__(0),
        lambda: labels.insert(0, "Inserted"),
        lambda: labels.append("Appended"),
        lambda: labels.extend(["Extended"]),
        labels.clear,
        labels.tolist,
        lambda: iter(labels),
    ]


def assert_stale(labels: SwitchLabels) -> None:
    for action in label_actions(labels):
        with pytest.raises(StaleHandleError):
            action()


def test_mutable_labels_and_snapshot_iteration() -> None:
    m = Module.create(build)
    try:
        q = switch(m)
        labels = q.labels
        assert labels is q.labels and list(labels) == ["Low", "Middle", "High"]
        assert labels[-1] == "High"
        labels[-1] = "Treble"
        del labels[-2]
        labels.insert(-1, "Center")
        labels.insert(-100, "Before")
        labels.insert(100, "After")
        assert q.labels.tolist() == ["Before", "Low", "Center", "Treble", "After"]
        snapshot = iter(labels)
        copied = labels.tolist()
        labels.clear()
        labels.extend(["One", "Two"])
        labels.append("Three")
        assert list(snapshot) == copied
        assert list(labels) == ["One", "Two", "Three"]
        # Retained alias survives whole-vector replacement and reallocation.
        q.labels = tuple(str(i) for i in range(1000))
        assert len(labels) == 1000 and labels[-1] == "999"
        q.labels = labels
        assert len(labels) == 1000
        q.labels = []
        assert len(labels) == 0
        labels.insert(-100, "First")
        labels.extend(labels)  # input snapshot makes self-extension well-defined
        assert labels.tolist() == ["First", "First"]
        del labels[-1]
        assert labels[0] == "First"
        for i in (-2, 1):
            for action in (
                lambda i=i: labels[i],
                lambda i=i: labels.__setitem__(i, "Bad"),
                lambda i=i: labels.__delitem__(i),
            ):
                with pytest.raises(IndexError):
                    action()
        bad_actions: list[Callable[[], Any]] = [
            lambda: labels.append(object()),  # type: ignore[arg-type]
            lambda: labels.insert(0, object()),  # type: ignore[arg-type]
            lambda: labels.extend(["Valid", object()]),  # type: ignore[list-item]
            lambda: labels.__setitem__(0, object()),  # type: ignore[assignment]
            lambda: setattr(q, "labels", ["Valid", object()]),
        ]
        for bad_action in bad_actions:
            with pytest.raises(TypeError):
                bad_action()
            assert labels.tolist() == ["First"]  # no partial writes
        after_close = iter(labels)
    finally:
        m.close()
    assert_stale(labels)
    assert list(after_close) == ["First"]
    assert copied == ["Before", "Low", "Center", "Treble", "After"]


def test_label_alias_replacement_rollback_and_owner_collection() -> None:
    escaped: list[SwitchLabels] = []

    def replace(m: Module) -> None:
        build(m)
        for _ in range(1000):
            previous = switch(m).labels
            m.config_switch(0, -1, 1, 0, labels=["New"])
            assert_stale(previous)
            assert switch(m).labels.tolist() == ["New"]
        escaped.append(switch(m).labels)

    m = Module.create(replace)
    del m  # aliases do not keep their module alive
    assert_stale(escaped[-1])

    def fail(m: Module) -> None:
        build(m)
        escaped.append(switch(m).labels)
        raise ValueError("after labels")

    with pytest.raises(ValueError, match="after labels"):
        Module.create(fail)
    assert_stale(escaped[-1])
    for _ in range(1000):
        m = Module.create(build)
        labels = switch(m).labels
        del m
        assert_stale(labels)


def test_label_conversion_reentry_rechecks_lifetime() -> None:
    for operation in ("extend", "replace"):
        m = Module.create(build)
        q = switch(m)
        labels = q.labels

        def closing(owner: Module = m) -> Iterator[str]:
            yield "Before close"
            owner.close()
            yield "After close"

        with pytest.raises(StaleHandleError):
            if operation == "extend":
                labels.extend(closing())  # type: ignore[arg-type]
            else:
                q.labels = closing()  # type: ignore[assignment]
        assert_stale(labels)


def test_label_alias_wrong_thread() -> None:
    m = Module.create(build)
    labels = switch(m).labels
    errors: list[BaseException] = []

    def worker() -> None:
        for action in label_actions(labels):
            try:
                action()
            except BaseException as e:
                errors.append(e)

    t = threading.Thread(target=worker)
    t.start()
    t.join()
    assert len(errors) == len(label_actions(labels)) and all(isinstance(e, ThreadError) for e in errors)
    assert labels.tolist() == ["Low", "Middle", "High"]
    m.close()


def test_helpers_native_differential_and_smoothing_refusal() -> None:
    # Startup context refusal is covered for every value helper in test_quantities.
    if _rackpy_host.bound_context_generation() is None:
        return
    from _rackpy_host import _testing

    for case in _testing.quantity_reference():

        def configure(m: Module, base: float = case["base"]) -> None:
            m.config(2, 0, 0)
            m.config_param(0, 1, 8, 2, "Pitch", " Hz", base, 3, 4)
            m.config_switch(1, -1, 1, 0, "Mode", ["Low", "Middle", "High"])

        m = Module.create(configure)
        try:
            q = m.param_quantities[0]
            assert q.get_range() == case["range"] and q.is_bounded() == case["bounded"]
            for x, scaled, unscaled in case["scaling"]:
                assert q.to_scaled(x) == pytest.approx(scaled)
                assert q.from_scaled(x) == pytest.approx(unscaled)
            q.set_immediate_value(2.5)
            for method, arg, value, scaled, is_min, is_max, text in case["helpers"]:
                if method in {"move_value", "set_scaled_value", "move_scaled_value"}:
                    getattr(q, method)(arg)
                else:
                    getattr(q, method)()
                assert q.get_value() == pytest.approx(value)
                assert q.get_scaled_value() == pytest.approx(scaled)
                assert (q.is_min(), q.is_max(), q.get_string()) == (is_min, is_max, text)
            s = m.param_quantities[1]
            assert isinstance(s, SwitchQuantityView)
            s.labels[0] = "Bass"
            del s.labels[1]
            s.labels.insert(1, "Center")
            s.labels.append("Extra")
            s.set_display_value_string("cENTER")
            assert [s.get_value(), s.get_display_value_string()] == case["edited_switch"]
            q.smooth_enabled = True
            for action in (
                q.set_min,
                q.set_max,
                q.toggle,
                partial(q.move_value, 1),
                partial(q.set_scaled_value, 1),
                partial(q.move_scaled_value, 1),
            ):
                before = q.get_immediate_value()
                with pytest.raises(OwnershipError, match="engine-owned"):
                    action()
                assert q.get_immediate_value() == before
            with pytest.raises(ContextError):
                _testing.with_foreign_context(q.get_string)
        finally:
            m.close()


def test_metadata_scaling_degenerate_and_unbounded() -> None:
    m = Module.create(build)
    try:
        q = m.param_quantities[0]
        q.min_value = q.max_value = 2
        assert q.get_range() == 0 and q.is_bounded()
        assert q.to_scaled(50) == 0 and q.from_scaled(0.5) == 2
        q.min_value = -float("inf")
        q.max_value = float("inf")
        assert not q.is_bounded()
        assert q.to_scaled(50) == q.from_scaled(50) == 50
    finally:
        m.close()
