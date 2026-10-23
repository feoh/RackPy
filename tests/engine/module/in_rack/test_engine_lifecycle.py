# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14C3: lifecycle events Rack's engine dispatches (live UI suite only).

The test hooks perform the engine operations a user's edits would; the
production start-up run ignores this file.
"""

from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.errors import OwnershipError
from rackpy.raw.engine import Module, ParamQuantity, Port

E = Module


def as_port(event: object) -> Module.PortChangeEvent:
    assert isinstance(event, Module.PortChangeEvent)
    return event


def as_rate(event: object) -> Module.SampleRateChangeEvent:
    assert isinstance(event, Module.SampleRateChangeEvent)
    return event


def kinds(events: list[tuple[int, Any]]) -> list[str]:
    return [type(event).__name__ for _, event in events]


def superclass_config(m: Module) -> None:
    # The same configuration as the hooks' plain rack::engine::Module.
    m.config(4, 0, 0, 0)

    class Labelled(ParamQuantity):
        def get_label(self) -> str:
            return "Labelled"

    m.config_param(0, 0, 10, 2, quantity_type=Labelled)
    m.config_switch(1, 0, 3, 1, "Mode", ["A", "B", "C", "D"])
    m.config_button(2)
    m.config_param(3, -5, 5, 0).randomize_enabled = False


def adopted(configure: Callable[[Module], object]) -> Module:
    m = Module.create(configure)
    HOOKS.engine_adopt(m)
    return m


def test_adding_reports_add_then_the_sample_rate() -> None:
    m = adopted(lambda m: m.config(0, 1, 1, 0))
    try:
        events = m.lifecycle_events()
        assert kinds(events) == ["AddEvent", "SampleRateChangeEvent"]
        assert as_rate(events[1][1]).sample_rate == HOOKS.engine_sample_rate()
        with pytest.raises(OwnershipError, match="engine dispatches"):
            m.on_reset(E.ResetEvent())
    finally:
        HOOKS.engine_delete(m)


def test_reset_and_randomize_keep_rack_superclass_behaviour() -> None:
    expected_reset, expected_random = HOOKS.superclass_reference(1234)
    m = adopted(superclass_config)
    try:
        m.lifecycle_events()
        for p in m.params:
            p.value = 1
        HOOKS.engine_reset(m)
        assert [p.value for p in m.params] == expected_reset
        HOOKS.seed_random(1234, 0)
        HOOKS.engine_randomize(m)  # RandomizeEvent, not ResetEvent
        assert [p.value for p in m.params] == expected_random
        assert kinds(m.lifecycle_events()) == ["ResetEvent", "RandomizeEvent"]
    finally:
        HOOKS.engine_delete(m)


def test_bypass_sample_rate_master_and_save() -> None:
    m = adopted(lambda m: m.config(0, 1, 1, 0))
    rate = HOOKS.engine_sample_rate()
    try:
        m.lifecycle_events()
        HOOKS.engine_bypass(m, True)
        assert m.is_bypassed()
        HOOKS.engine_bypass(m, True)  # no change, no event
        HOOKS.engine_bypass(m, False)
        assert not m.is_bypassed()
        HOOKS.engine_set_sample_rate(96000)
        HOOKS.engine_set_master(m)
        HOOKS.engine_set_master(None)
        HOOKS.engine_prepare_save()
        events = m.lifecycle_events()
        assert kinds(events) == [
            "BypassEvent",
            "UnBypassEvent",
            "SampleRateChangeEvent",
            "SetMasterEvent",
            "UnsetMasterEvent",
            "SaveEvent",
        ]
        rate_event = as_rate(events[2][1])
        assert rate_event.sample_rate == 96000 and rate_event.sample_time == pytest.approx(1 / 96000)
    finally:
        HOOKS.engine_set_sample_rate(rate)
        HOOKS.engine_delete(m)
    assert HOOKS.engine_sample_rate() == rate


def test_cables_report_port_changes_on_both_modules() -> None:
    source = adopted(lambda m: m.config(0, 0, 2, 0))
    target = adopted(lambda m: m.config(0, 3, 0, 0))
    try:
        source.lifecycle_events(), target.lifecycle_events()
        cable = HOOKS.engine_connect(source, 1, target, 2)
        HOOKS.engine_disconnect(cable)
        out_events = [as_port(event) for _, event in source.lifecycle_events()]
        in_events = [as_port(event) for _, event in target.lifecycle_events()]
        assert [(e.connecting, e.type, e.port_id) for e in out_events] == [
            (True, Port.Type.OUTPUT, 1),
            (False, Port.Type.OUTPUT, 1),
        ]
        assert [(e.connecting, e.type, e.port_id) for e in in_events] == [
            (True, Port.Type.INPUT, 2),
            (False, Port.Type.INPUT, 2),
        ]
    finally:
        HOOKS.engine_delete(source)
        HOOKS.engine_delete(target)


def test_removal_is_observed_before_rack_deletes_the_module() -> None:
    m = adopted(lambda m: m.config(0, 0, 0, 0))
    m.lifecycle_events()
    seen: list[list[str]] = []
    HOOKS.engine_delete(m, removed=lambda: seen.append(kinds(m.lifecycle_events())))
    assert seen == [["RemoveEvent"]] and not m.alive


def test_python_inside_an_exclusive_event_cannot_lock_the_engine() -> None:
    outcomes: list[str] = []

    class Reentrant(ParamQuantity):
        def reset(self) -> None:
            try:
                HOOKS.engine_module_count()  # takes Rack's engine lock
                outcomes.append("locked")
            except Exception as error:
                outcomes.append(type(error).__name__)
            super().reset()

    def configure(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 1, 1, quantity_type=Reentrant)

    m = adopted(configure)
    try:
        m.params[0].value = 0
        raised = _rackpy_host.dispatch_stats()["raised"]
        HOOKS.engine_reset(m)
        assert outcomes == ["EngineLockError"] and m.params[0].value == 1
        assert _rackpy_host.dispatch_stats()["raised"] == raised
    finally:
        HOOKS.engine_delete(m)
