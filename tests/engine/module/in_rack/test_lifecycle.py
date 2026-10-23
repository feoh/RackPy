# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14C3: Rack's lifecycle event records, direct dispatch and observed snapshots."""

import threading
import warnings
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest

from rackpy.errors import ContextError, OwnershipError, ThreadError
from rackpy.raw.engine import Module, ParamQuantity, Port

LIVE = _rackpy_host.bound_context_generation() is not None
E = Module


def as_port(event: object) -> Module.PortChangeEvent:
    assert isinstance(event, Module.PortChangeEvent)
    return event


def as_rate(event: object) -> Module.SampleRateChangeEvent:
    assert isinstance(event, Module.SampleRateChangeEvent)
    return event


def kinds(events: list[tuple[int, Any]]) -> list[str]:
    return [type(event).__name__ for _, event in events]


def build(configure: Callable[[Module], object] | None = None) -> Module:
    def run(m: Module) -> None:
        m.config(3, 2, 2, 0)
        m.config_param(0, 0, 10, 2)
        m.config_switch(1, 0, 3, 1, "Mode", ["A", "B", "C", "D"])
        m.config_button(2)
        if configure:
            configure(m)

    return Module.create(run)


def test_event_records_are_rack_values() -> None:
    port = E.PortChangeEvent(connecting=True, type=Port.Type.OUTPUT, port_id=3)
    assert (port.connecting, port.type, port.port_id) == (True, Port.Type.OUTPUT, 3)
    assert repr(port) == "Module.PortChangeEvent(connecting=True, type=OUTPUT, port_id=3)"
    port.port_id = 4
    assert port.port_id == 4
    rate = E.SampleRateChangeEvent()
    assert rate.sample_rate == 44100 and rate.sample_time == pytest.approx(1 / 44100)
    rate = E.SampleRateChangeEvent(48000, 1 / 48000)
    assert repr(rate).startswith("Module.SampleRateChangeEvent(sample_rate=48000")
    plain: list[type] = [
        E.AddEvent,
        E.RemoveEvent,
        E.BypassEvent,
        E.UnBypassEvent,
        E.ResetEvent,
        E.RandomizeEvent,
        E.SaveEvent,
        E.SetMasterEvent,
        E.UnsetMasterEvent,
    ]
    for cls in plain:
        assert repr(cls()) == f"Module.{cls.__name__}()"


def test_direct_dispatch_needs_a_sealed_module_and_rack_context() -> None:
    def early(m: Module) -> None:
        m.config(0, 0, 0)
        with pytest.raises(OwnershipError, match="after the constructor callback"):
            m.on_add(E.AddEvent())

    Module.create(early).close()
    m = build()
    try:
        assert m.lifecycle_events() == [] and m.dropped_lifecycle_events == 0
        if not LIVE:
            dispatches: list[Callable[[], object]] = [
                lambda: m.on_reset(E.ResetEvent()),
                lambda: m.on_add(E.AddEvent()),
            ]
            for dispatch in dispatches:
                with pytest.raises(ContextError, match="live bound"):
                    dispatch()
            assert m.lifecycle_events() == []
    finally:
        m.close()


def test_direct_dispatch_runs_rack_behaviour_and_records_snapshots() -> None:
    if not LIVE:
        return  # needs Rack's live engine for the parameter writes
    calls: list[str] = []

    class Resetting(ParamQuantity):
        def reset(self) -> None:
            calls.append("reset")
            super().reset()

    m = build(lambda m: m.config_param(0, 0, 10, 2, quantity_type=Resetting))
    try:
        for p in m.params:
            p.value = 1
        m.on_reset(E.ResetEvent())  # Rack's default: reset every quantity
        assert [p.value for p in m.params] == [2, 1, 0] and calls == ["reset"]
        m.on_add(E.AddEvent())
        m.on_port_change(E.PortChangeEvent(True, Port.Type.OUTPUT, 1))
        m.on_sample_rate_change(E.SampleRateChangeEvent(48000, 1 / 48000))
        m.on_bypass(E.BypassEvent())
        m.on_un_bypass(E.UnBypassEvent())
        m.on_save(E.SaveEvent())
        m.on_set_master(E.SetMasterEvent())
        m.on_unset_master(E.UnsetMasterEvent())
        m.on_remove(E.RemoveEvent())
        m.on_randomize(E.RandomizeEvent())
        assert 0 <= m.params[0].value <= 10 and m.params[1].value in (0, 1, 2, 3) and m.params[2].value == 0
        events = m.lifecycle_events()
        assert kinds(events) == [
            "ResetEvent",
            "AddEvent",
            "PortChangeEvent",
            "SampleRateChangeEvent",
            "BypassEvent",
            "UnBypassEvent",
            "SaveEvent",
            "SetMasterEvent",
            "UnsetMasterEvent",
            "RemoveEvent",
            "RandomizeEvent",
        ]
        assert [sequence for sequence, _ in events] == list(range(1, 12))
        port, rate = as_port(events[2][1]), as_rate(events[3][1])
        assert (port.connecting, port.type, port.port_id) == (True, Port.Type.OUTPUT, 1)
        assert rate.sample_rate == 48000 and rate.sample_time == pytest.approx(1 / 48000)
        assert m.lifecycle_events() == []  # drained
        # Rack's deprecated no-argument events are empty: nothing to record.
        for name in ("on_add", "on_remove", "on_reset", "on_randomize", "on_sample_rate_change"):
            with pytest.warns(DeprecationWarning, match="deprecated in Rack"):
                getattr(m, name)()
        assert m.lifecycle_events() == []
    finally:
        m.close()


def test_a_full_queue_drops_and_counts_new_events() -> None:
    if not LIVE:
        return
    m = build()
    try:
        for _ in range(100):
            m.on_bypass(E.BypassEvent())
        events = m.lifecycle_events()
        assert len(events) == 64 and m.dropped_lifecycle_events == 36
        assert [sequence for sequence, _ in events] == list(range(1, 65))
        m.on_save(E.SaveEvent())
        # Dropped events kept their sequence numbers: the gap shows the loss.
        assert [sequence for sequence, _ in m.lifecycle_events()] == [101]
    finally:
        m.close()


def test_lifecycle_main_thread_rule() -> None:
    m = build()
    actions: list[Callable[[], object]] = [
        m.lifecycle_events,
        lambda: m.dropped_lifecycle_events,
        lambda: m.on_add(E.AddEvent()),
        lambda: m.on_reset(E.ResetEvent()),
    ]
    errors: list[BaseException] = []

    def worker() -> None:
        with warnings.catch_warnings():
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
