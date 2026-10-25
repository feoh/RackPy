# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-16C: Python handlers for Rack's module events, dispatched by Rack itself (live UI suite only).

The production start-up run ignores this file: it needs the test hooks and Rack's engine.
"""

import time
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.errors import EngineLockError, OwnershipError
from rackpy.plugin import ModuleClass
from rackpy.raw import plugin as rp
from rackpy.raw.engine import Module


def recorder(seen: list[tuple[str, Any]]) -> dict[str, Any]:
    names = (
        "on_add",
        "on_remove",
        "on_bypass",
        "on_un_bypass",
        "on_port_change",
        "on_reset",
        "on_randomize",
        "on_save",
    )
    return {name: (lambda event, name=name: seen.append((name, event))) for name in names}


def test_rack_dispatches_events_to_python_after_its_own_behaviour() -> None:
    seen: list[tuple[str, Any]] = []
    values: list[float] = []

    def build(m: Module) -> None:
        m.config(1, 1, 0, 0)
        m.config_param(0, 0, 10, 3, "Level")
        handlers = recorder(seen)

        def on_reset(event: object) -> None:
            seen.append(("on_reset", event))
            values.append(m.params[0].value)

        handlers["on_reset"] = on_reset
        m.set_event_handlers(**handlers)

    m = Module.create(build)
    source = Module.create(lambda s: s.config(0, 0, 1, 0))
    HOOKS.engine_adopt(source)
    HOOKS.engine_adopt(m)
    try:
        m.params[0].value = 7
        HOOKS.engine_bypass(m, True)
        HOOKS.engine_bypass(m, False)
        cable = HOOKS.engine_connect(source, 0, m, 0)
        HOOKS.engine_disconnect(cable)
        HOOKS.engine_reset(m)
        HOOKS.engine_randomize(m)
        HOOKS.engine_prepare_save()
    finally:
        HOOKS.engine_delete(m, None)
        HOOKS.engine_delete(source, None)
    names = [name for name, _ in seen]
    assert names == [
        "on_add",
        "on_bypass",
        "on_un_bypass",
        "on_port_change",
        "on_port_change",
        "on_reset",
        "on_randomize",
        "on_save",
        "on_remove",
    ]
    connect, disconnect = seen[3][1], seen[4][1]
    assert isinstance(connect, Module.PortChangeEvent) and connect.connecting and connect.port_id == 0
    assert not disconnect.connecting
    assert values == [3.0]  # Rack had already reset the parameter when on_reset ran


def test_handler_errors_are_contained_and_slow_ones_are_diagnosed() -> None:
    def fail(event: object) -> None:
        raise RuntimeError("event-handler-intentional-error")

    def slow(event: object) -> None:
        time.sleep(0.005)

    engine_calls: list[type[BaseException]] = []

    def reenter(event: object) -> None:
        try:
            HOOKS.engine_sample_rate()  # takes the engine lock Rack already holds
        except EngineLockError as error:
            engine_calls.append(type(error))

    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 3)
        m.set_event_handlers(on_randomize=fail, on_reset=slow, on_bypass=reenter)

    m = Module.create(build)
    HOOKS.engine_adopt(m)
    try:
        raised, sequence = _rackpy_host.dispatch_stats()["raised"], _rackpy_host.diagnostics_recorded()
        m.params[0].value = 9
        HOOKS.engine_randomize(m)  # Rack's randomize still happened
        assert _rackpy_host.dispatch_stats()["raised"] == raised + 1
        HOOKS.engine_reset(m)
        assert m.params[0].value == 3
        codes = [code for _, _, code, _, message in _rackpy_host.diagnostics(sequence)]
        assert "slow_exclusive_callback" in codes
        HOOKS.engine_bypass(m, True)
        assert engine_calls == [EngineLockError]
    finally:
        HOOKS.engine_delete(m, None)


def test_handlers_are_constructor_only_and_checked() -> None:
    def build(m: Module) -> None:
        m.config(0, 0, 0, 0)
        with pytest.raises(TypeError, match="unexpected event on_sample_rate_change"):
            m.set_event_handlers(on_sample_rate_change=print)  # type: ignore[call-arg]
        with pytest.raises(TypeError, match="on_reset must be callable"):
            m.set_event_handlers(on_reset=42)  # type: ignore[arg-type]
        m.set_event_handlers(on_reset=print, on_save=None)

    m = Module.create(build)
    try:
        with pytest.raises(OwnershipError, match="constructor callback"):
            m.set_event_handlers(on_reset=print)
    finally:
        m.close()


def test_module_class_installs_only_what_it_overrides() -> None:
    seen: list[str] = []

    class Counter(ModuleClass):
        def configure(self) -> None:
            self.module.config(1, 0, 0, 0)

        def on_reset(self, event: Any) -> None:
            seen.append(type(event).__name__)

    m = Module.create(Counter.factory())
    HOOKS.engine_adopt(m)
    try:
        HOOKS.engine_reset(m)
        HOOKS.engine_randomize(m)  # not overridden, so nothing is called
        m_model = m.model
        assert m_model is not None and m_model is m.get_model() and m_model is rp.get_model("RackPy", "RuntimeProbe")
    finally:
        HOOKS.engine_delete(m, None)
    assert seen == ["ResetEvent"]
    plain = Module.create()
    try:
        assert plain.model is None and plain.get_model() is None
    finally:
        plain.close()


def test_direct_dispatch_from_python_reaches_handlers() -> None:
    seen: list[tuple[str, Any]] = []

    def build(m: Module) -> None:
        m.config(0, 0, 0, 0)
        m.set_event_handlers(**recorder(seen))

    m = Module.create(build)
    try:
        m.on_reset(Module.ResetEvent())
        m.on_save(Module.SaveEvent())
        assert [name for name, _ in seen] == ["on_reset", "on_save"]
    finally:
        m.close()
