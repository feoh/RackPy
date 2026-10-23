# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14B2b5: class factories transfer Python metadata into module slots."""

import gc
import threading
import weakref
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest

from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import LightInfo, LightInfoView, Module, Port, PortInfo, PortInfoView

HOOKS = getattr(_rackpy_host, "_testing", None)


class CustomPort(PortInfo):
    def __init__(self) -> None:
        super().__init__()
        self.description = "From constructor"

    def get_name(self) -> str:
        return "Custom " + self.name


class CustomLight(LightInfo):
    def __init__(self) -> None:
        super().__init__()
        self.description = "From constructor"

    def get_name(self) -> str:
        return "Custom " + self.name


def build(configure: Callable[[Module], object], counts: tuple[int, int, int, int] = (0, 1, 1, 1)) -> Module:
    def run(m: Module) -> None:
        m.config(*counts)
        configure(m)

    return Module.create(run)


def bound_actions(info: PortInfo | LightInfo) -> list[Callable[[], object]]:
    actions: list[Callable[[], object]] = [
        lambda: info.module,
        lambda: setattr(info, "module", None),
        lambda: info.name,
        lambda: setattr(info, "name", "x"),
        lambda: info.description,
        info.get_name,
        info.get_description,
    ]
    if isinstance(info, PortInfo):
        actions += [lambda: info.port_id, lambda: info.type, info.get_full_name]
    else:
        actions.append(lambda: info.light_id)
    return actions


def assert_stale(info: PortInfo | LightInfo, match: str = "replaced or its module was deleted") -> None:
    assert not info.alive
    for action in bound_actions(info):
        with pytest.raises(StaleHandleError, match=match):
            action()


def test_factories_match_native_reference(reference: dict[str, Any]) -> None:
    made: dict[str, PortInfo | LightInfo] = {}

    def configure(m: Module) -> None:
        made["input"] = m.config_input(0, "Pitch", info_type=CustomPort)
        made["output"] = m.config_output(0, "Out", info_type=CustomPort)
        made["light"] = m.config_light(0, "Gate", info_type=CustomLight)

    baseline = _rackpy_host.transferred_objects()
    m = build(configure)
    try:
        inp, out, light = made["input"], made["output"], made["light"]
        assert isinstance(inp, PortInfo) and isinstance(out, PortInfo) and isinstance(light, LightInfo)
        assert type(inp) is CustomPort and type(light) is CustomLight
        observed = []
        for port, slot in ((inp, m.get_input_info(0)), (out, m.get_output_info(0))):
            assert isinstance(port, PortInfo)
            observed.append(
                [
                    port.get_name(),
                    port.get_description(),
                    port.name,
                    port.port_id,
                    port.type == Port.Type.INPUT,
                    port.module is m,
                    port is slot,
                ]
            )
        observed.append(
            [
                light.get_name(),
                light.get_description(),
                light.name,
                light.light_id,
                False,
                light.module is m,
                light is m.get_light_info(0),
            ]
        )
        assert observed == reference["factories"]
        assert inp is m.input_infos[0] and out is next(iter(m.output_infos)) and light is m.light_infos[-1]
        assert isinstance(inp, PortInfo) and inp.get_full_name() == "Custom Pitch input"
        assert isinstance(out, PortInfo) and out.type == Port.Type.OUTPUT
        assert inp.alive and light.alive
        assert _rackpy_host.transferred_objects() == baseline + 3
    finally:
        m.close()
    assert _rackpy_host.transferred_objects() == baseline


def test_replacement_retires_and_matches_native_reference(reference: dict[str, Any]) -> None:
    observed: list[object] = []
    finalized: list[str] = []

    def configure(m: Module) -> None:
        first = m.config_input(0, "First", info_type=CustomPort)
        weakref.finalize(first, finalized.append, "first")
        again = m.config_input(0, "Again", info_type=CustomPort)
        observed.extend([[first.alive, again.alive].count(False), m.get_input_info(0) is again, again.get_name()])
        # The replaced object is Python-owned again but stale, like Rack's deleted pointer.
        assert_stale(first)
        del first
        gc.collect()
        assert finalized == ["first"]
        plain = m.config_input(0, "Plain")
        assert type(plain) is PortInfoView
        observed.extend([[again.alive].count(False) + 1, plain.get_name()])

    m = build(configure)
    try:
        assert observed == reference["factory_replacement"]
    finally:
        m.close()


def test_close_gc_and_rollback_retire_without_retaining_owner() -> None:
    held: list[PortInfo | LightInfo] = []

    def configure(m: Module) -> None:
        held.append(m.config_input(0, "In", info_type=CustomPort))
        held.append(m.config_light(0, "Light", info_type=CustomLight))

    baseline = _rackpy_host.transferred_objects()
    m = build(configure)
    owner = weakref.ref(m)
    m.close()
    for info in held:
        assert_stale(info)
    assert _rackpy_host.transferred_objects() == baseline

    # Dropping the last module reference retires its metadata, too.
    held.clear()
    m = build(configure)
    owner = weakref.ref(m)
    del m
    gc.collect()
    assert owner() is None and held and not any(info.alive for info in held)
    assert _rackpy_host.transferred_objects() == baseline

    # The last handle may also go on a Python worker: retirement happens there, with the GIL.
    held.clear()
    pending = [build(configure)]
    thread = threading.Thread(target=pending.clear)
    thread.start()
    thread.join()
    assert len(held) == 2 and not any(info.alive for info in held)
    assert _rackpy_host.transferred_objects() == baseline

    # A failed constructor retires what it transferred before raising.
    held.clear()

    def failing(m: Module) -> None:
        configure(m)
        raise ValueError("rollback transferred metadata")

    with pytest.raises(ValueError, match="rollback transferred"):
        build(failing)
    assert len(held) == 2 and not any(info.alive for info in held)
    assert _rackpy_host.transferred_objects() == baseline


def test_class_and_instance_errors_leave_slot_unchanged() -> None:
    class Failing(PortInfo):
        def __init__(self) -> None:
            super().__init__()
            raise RuntimeError("constructor failed")

    class Foreign(PortInfo):
        def __new__(cls) -> Foreign:
            return CustomPort()  # type: ignore[return-value]

    class NoInit(PortInfo):
        def __init__(self) -> None:
            pass

    def configure(m: Module) -> None:
        view = m.get_input_info(0)
        before = view.get_name()
        bad_types: list[Any] = [object, PortInfoView, LightInfo, CustomLight, CustomPort(), "PortInfo"]
        for bad in bad_types:
            with pytest.raises(TypeError, match="info_type must be PortInfo"):
                m.config_input(0, "Bad", info_type=bad)
        with pytest.raises(TypeError, match="info_type must be LightInfo"):
            m.config_light(0, "Bad", info_type=PortInfo)  # type: ignore[type-var]
        with pytest.raises(RuntimeError, match="constructor failed"):
            m.config_input(0, "Bad", info_type=Failing)
        with pytest.raises(TypeError, match="exactly info_type"):
            m.config_input(0, "Bad", info_type=Foreign)
        with pytest.raises(TypeError, match="__init__"):
            m.config_input(0, "Bad", info_type=NoInit)
        for i in (-1, 1):
            with pytest.raises(IndexError):
                m.config_input(i, "Bad", info_type=CustomPort)
        assert m.get_input_info(0) is view and view.alive and view.get_name() == before

    baseline = _rackpy_host.transferred_objects()
    m = build(configure)
    m.close()
    assert _rackpy_host.transferred_objects() == baseline


def test_exact_type_reuse_and_phase_errors() -> None:
    seen: list[PortInfo] = []

    class Reused(PortInfo):
        def __new__(cls) -> Reused:
            if seen:
                return seen[0]  # type: ignore[return-value]
            return super().__new__(cls)

    def configure(m: Module) -> None:
        seen.append(m.config_input(0, "Reused", info_type=Reused))
        with pytest.raises(OwnershipError, match="already owns"):
            m.config_output(0, "Reused", info_type=Reused)
        m.config_input(0, "Replaced")
        assert not seen[0].alive
        with pytest.raises(StaleHandleError, match="already released"):
            m.config_output(0, "Reused", info_type=Reused)
        assert type(m.get_output_info(0)) is PortInfoView

    m = build(configure)
    try:
        with pytest.raises(OwnershipError, match="constructor callback"):
            m.config_input(0, "Late", info_type=CustomPort)
    finally:
        m.close()
    with pytest.raises(StaleHandleError):
        m.config_input(0, "Closed", info_type=CustomPort)


def test_reentrant_constructor_replaces_safely() -> None:
    inner: list[PortInfo] = []
    module: list[Module] = []

    class Reentrant(PortInfo):
        def __init__(self) -> None:
            super().__init__()
            if not inner:
                inner.append(module[0].config_input(0, "Inner", info_type=CustomPort))

    class Closing(PortInfo):
        def __init__(self) -> None:
            super().__init__()
            with pytest.raises(OwnershipError):
                module[0].close()

    def configure(m: Module) -> None:
        module.append(m)
        outer = m.config_input(0, "Outer", info_type=Reentrant)
        assert not inner[0].alive and outer.alive and m.get_input_info(0) is outer
        assert outer.name == "Outer" and inner[0] is not outer
        m.config_light(0, "Closing", info_type=LightInfo)
        assert m.config_output(0, "Closing", info_type=Closing).alive

    m = build(configure)
    m.close()


def test_transferred_module_field_and_overrides() -> None:
    errors: list[str] = []

    class Erroring(PortInfo):
        def get_description(self) -> str:
            errors.append("description")
            raise ValueError("description failed")

    class Lit(LightInfo):
        def get_name(self) -> str:
            assert self.module is not None
            return "Lit"

    other = Module.create()

    def configure(m: Module) -> None:
        info = m.config_input(0, "In", info_type=Erroring)
        with pytest.raises(ValueError, match="description failed"):
            info.get_description()
        info.module = None
        assert info.module is None
        info.module = other
        assert info.module is other
        info.module = m
        assert info.module is m
        m.config_light(0, "Light", info_type=Lit)

    m = build(configure)
    try:
        info = m.get_input_info(0)
        light = m.get_light_info(0)
        assert isinstance(info, Erroring) and isinstance(light, Lit) and light.get_name() == "Lit"
        if HOOKS is not None:
            # From Rack (no GIL), a failing override falls back to Rack's answer.
            assert HOOKS.metadata_as_rack(m, "input", 0) == ("In input", "")
            assert HOOKS.metadata_as_rack(m, "light", 0) == ("Lit", "")
            assert errors == ["description", "description"]
        other.close()
        assert info.module is m
    finally:
        m.close()
        other.close()


def test_transferred_metadata_main_thread_rule() -> None:
    def configure(m: Module) -> None:
        m.config_input(0, "In", info_type=CustomPort)
        m.config_light(0, "Light", info_type=CustomLight)

    m = build(configure)
    infos: list[PortInfo | LightInfo] = []
    for info in (m.get_input_info(0), m.get_light_info(0)):
        assert isinstance(info, (PortInfo, LightInfo))
        infos.append(info)
    outcomes: list[BaseException] = []
    alive: list[bool] = []

    def worker() -> None:
        for info in infos:
            alive.append(info.alive)
            for action in bound_actions(info):
                try:
                    action()
                except BaseException as error:
                    outcomes.append(error)

    try:
        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        expected = sum(len(bound_actions(info)) for info in infos)
        assert alive == [True, True]
        assert len(outcomes) == expected and all(isinstance(e, ThreadError) for e in outcomes)
        # Standalone metadata keeps its any-thread fields.
        standalone = CustomPort()
        thread = threading.Thread(target=lambda: alive.append(standalone.get_name() == "Custom "))
        thread.start()
        thread.join()
        assert alive[-1]
    finally:
        m.close()


def test_one_thousand_replacements_and_owner_churns() -> None:
    baseline = _rackpy_host.transferred_objects()
    retired: weakref.WeakSet[PortInfo] = weakref.WeakSet()
    owners: weakref.WeakSet[Module] = weakref.WeakSet()

    def configure(m: Module) -> None:
        for i in range(1000):
            info = m.config_input(0, str(i), info_type=CustomPort)
            retired.add(info)
        assert _rackpy_host.transferred_objects() == baseline + 1

    m = build(configure)
    gc.collect()
    assert len(retired) == 1 and next(iter(retired)) is m.get_input_info(0)
    m.close()

    for i in range(1000):
        owner = build(lambda m: m.config_light(0, "L", info_type=CustomLight))
        owners.add(owner)
        light = owner.get_light_info(0)
        assert isinstance(light, LightInfo)
        if i % 2:
            owner.close()
        del owner
        assert not light.alive
    gc.collect()
    assert not owners and not retired
    assert _rackpy_host.transferred_objects() == baseline


def test_builtin_views_are_unchanged() -> None:
    m = build(lambda m: (m.config_input(0, "In"), m.config_light(0, "L")))
    try:
        assert type(m.get_input_info(0)) is PortInfoView and type(m.get_light_info(0)) is LightInfoView
    finally:
        m.close()
