# SPDX-License-Identifier: GPL-3.0-or-later
"""Built-in metadata adapters: real Rack, not complete template semantics."""

import gc
import threading
import weakref
from collections.abc import Callable
from typing import Any

import pytest

from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import LightInfoView, Module, Port, PortInfo, PortInfoView


def test_metadata_matches_native_reference(reference: dict[str, Any]) -> None:
    for case in reference["metadata"]:

        def configure(m: Module, case: dict[str, Any] = case) -> None:
            m.config(0, 2, 2, 2)
            assert [
                [m.get_input_info(i).get_name(), m.get_output_info(i).get_name(), m.get_light_info(i) is None]
                for i in range(2)
            ] == case["defaults"]
            for i in range(2):
                inp = m.config_input(i, case["name"])
                out = m.config_output(i, case["name"])
                light = m.config_light(i, case["name"])
                inp.description = out.description = light.description = "A description"

        m = Module.create(configure)
        try:
            for i, expected in enumerate(case["configured"]):
                inp, out, light = m.get_input_info(i), m.get_output_info(i), m.get_light_info(i)
                assert light is not None
                assert [
                    inp.port_id,
                    inp.get_name(),
                    inp.get_description(),
                    out.get_name(),
                    out.get_description(),
                    light.get_name(),
                    light.get_description(),
                    inp.module is m,
                    out.module is m,
                    light.module is m,
                    inp.type == Port.Type.INPUT,
                    out.type == Port.Type.OUTPUT,
                ] == expected
                # Full names require Rack's translations, not standalone libRack.
                assert inp.get_full_name() == inp.get_name() + " input"
                assert out.get_full_name() == out.get_name() + " output"
                assert out.port_id == light.light_id == i
                assert inp is m.input_infos[i] and out is m.output_infos[i] and light is m.light_infos[i]
                assert list(m.input_infos)[i] is inp
                assert isinstance(inp, PortInfoView) and isinstance(light, LightInfoView)
                # Distinct adapters: no claim to map standalone subclass factories.
                assert type(inp) is PortInfoView
                inp.name = out.name = light.name = "Renamed"
                assert inp.get_full_name() == "Renamed input"
                assert out.get_full_name() == "Renamed output"
                assert light.get_name() == "Renamed"
        finally:
            m.close()


def test_null_slots_indices_phase_and_argument_errors() -> None:
    empty = Module.create()
    assert not list(empty.input_infos) and not list(empty.output_infos) and not list(empty.light_infos)
    empty.close()

    def configure(m: Module) -> None:
        with pytest.raises(IndexError):
            m.config_light(0)
        m.config(0, 1, 1, 2)
        assert m.light_infos[0] is m.light_infos[-1] is None
        for method in (m.config_input, m.config_output, m.config_light):
            for i in (-1, 2):
                with pytest.raises(IndexError):
                    method(i)
        before = m.get_input_info(0)
        with pytest.raises(TypeError):
            m.config_input(0, object())  # type: ignore[call-overload]
        with pytest.raises(TypeError):
            m.config_input(0, info_class=PortInfo)  # type: ignore[call-overload]
        assert m.get_input_info(0) is before and before.alive
        # A failed conversion didn't delete or change the existing metadata.
        assert before.name == ""
        m.config_light(0)

    m = Module.create(configure)
    for method in (m.config_input, m.config_output, m.config_light):
        with pytest.raises(OwnershipError, match="constructor"):
            method(0)
    for group, getter in ((m.input_infos, m.get_input_info), (m.output_infos, m.get_output_info)):
        assert group[-1] is getter(0)
        for i in (-2, 1):
            with pytest.raises(IndexError):
                _ = group[i]
        with pytest.raises(TypeError):
            group[0] = group[0]  # type: ignore[index]
    for info_getter in (m.get_input_info, m.get_output_info, m.get_light_info):
        for i in (-1, 2):
            with pytest.raises(IndexError):
                info_getter(i)
    assert m.get_light_info(1) is None
    inp = m.get_input_info(0)
    for name, value in (("port_id", 2), ("type", Port.Type.OUTPUT), ("module", None)):
        with pytest.raises(AttributeError):
            setattr(inp, name, value)
    m.close()


def accesses(view: Any) -> list[Callable[[], Any]]:
    actions: list[Callable[[], Any]] = [
        lambda: view.name,
        lambda: setattr(view, "name", "unsafe"),
        lambda: view.description,
        lambda: setattr(view, "description", "unsafe"),
        view.get_name,
        view.get_description,
        lambda: view.module,
    ]
    if isinstance(view, PortInfoView):
        actions.extend((lambda: view.type, lambda: view.port_id, view.get_full_name))
    else:
        actions.append(lambda: view.light_id)
    return actions


def assert_stale(view: Any) -> None:
    assert not view.alive
    for action in accesses(view):
        with pytest.raises(StaleHandleError):
            action()


def test_replacement_invalidates_only_replaced_slot_and_churn() -> None:
    escaped: list[Any] = []

    def configure(m: Module) -> None:
        m.config(0, 2, 1, 1)
        other = m.input_infos[1]
        for _ in range(1000):
            old = m.get_input_info(0)
            new = m.config_input(0, "Replacement")
            assert new is not old and new is m.input_infos[0]
            assert_stale(old)
            assert other.alive and other.module is m
        for getter, config in ((m.get_output_info, m.config_output), (m.get_light_info, m.config_light)):
            old_info = config(0, "First")
            new_info = config(0)  # default empty name/description, not reused metadata
            assert_stale(old_info)
            assert new_info is getter(0) and new_info.name == new_info.description == ""
            escaped.append(new_info)
        escaped.extend((m.get_input_info(0), other))

    m = Module.create(configure)
    assert all(view.alive for view in escaped)
    m.close()
    for view in escaped:
        assert_stale(view)


def test_constructor_failure_and_gc_invalidate_without_retaining_owner() -> None:
    escaped: list[Any] = []

    def configure(m: Module) -> None:
        m.config(0, 1, 1, 1)
        escaped.extend((m.input_infos, m.input_infos[0], m.config_output(0), m.config_light(0)))
        raise ValueError("failure after metadata configuration")

    with pytest.raises(ValueError, match="after metadata"):
        Module.create(configure)
    with pytest.raises(StaleHandleError):
        len(escaped[0])
    for view in escaped[1:]:
        assert_stale(view)

    seen: weakref.WeakSet[Module] = weakref.WeakSet()
    for _ in range(1000):
        m = Module.create(lambda m: m.config(0, 1, 1, 1))
        seen.add(m)
        view, group = m.input_infos[0], m.light_infos
        del m
        assert_stale(view)
        with pytest.raises(StaleHandleError):
            _ = group[0]  # even a null slot must check its module's lifetime
    gc.collect()
    assert not seen


def test_metadata_main_thread_rule() -> None:
    def configure(m: Module) -> None:
        m.config(0, 1, 1, 1)
        m.config_light(0)

    m = Module.create(configure)
    group, null_group = m.input_infos, m.light_infos
    views = m.input_infos[0], m.output_infos[0], m.light_infos[0]
    actions: list[Callable[[], Any]] = [
        lambda: m.input_infos,
        lambda: len(group),
        lambda: null_group[0],
        lambda: m.get_input_info(0),
        lambda: m.get_output_info(0),
        lambda: m.get_light_info(0),
        lambda: m.config_input(0),
        lambda: m.config_output(0),
        lambda: m.config_light(0),
    ]
    for view in views:
        actions.extend(accesses(view))
    outcomes: list[BaseException] = []

    def worker() -> None:
        for action in actions:
            try:
                action()
            except BaseException as e:
                outcomes.append(e)

    t = threading.Thread(target=worker)
    t.start()
    t.join()
    assert len(outcomes) == len(actions) and all(isinstance(e, ThreadError) for e in outcomes)
    assert m.get_input_info(0).name == ""
    m.close()
