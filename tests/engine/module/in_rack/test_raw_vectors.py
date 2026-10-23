# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-14B2b7: constructor-scoped raw edits of a module's vectors, checked at the seal."""

import gc
import threading
import weakref
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest

from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import (
    Input,
    Light,
    LightInfo,
    LightInfoView,
    Module,
    Output,
    Param,
    ParamQuantity,
    ParamQuantityView,
    Port,
    PortInfo,
    PortInfoView,
    SwitchQuantity,
)


def build(configure: Callable[[Module], object], counts: tuple[int, int, int, int] = (2, 2, 1, 1)) -> Module:
    def run(m: Module) -> None:
        m.config(*counts)
        configure(m)

    return Module.create(run)


def alive(handle: Any) -> bool:
    return bool(handle.alive)  # native edits change this behind mypy's narrowing


def module_of(info: PortInfo) -> Module | None:
    return info.module


def port_info(m: Module | None, port_id: int, type: Port.Type, name: str = "") -> PortInfo:
    info = PortInfo()
    info.module, info.port_id, info.type, info.name = m, port_id, type, name
    return info


def test_raw_edits_match_native_reference(reference: dict[str, Any]) -> None:
    def configure(m: Module) -> None:
        m.params.append(Param())
        m.param_quantities.append(None)
        m.config_param(2, 0, 5, 1, "Added")
        del m.inputs[0]
        del m.input_infos[0]
        m.input_infos[0].port_id = 0  # the shifted metadata keeps its old ID until fixed
        m.lights.resize(3)
        m.light_infos.resize(3)
        m.output_infos[0] = port_info(m, 0, Port.Type.OUTPUT, "Raw")
        m.params[0].value = 0.25
        m.params[1] = m.params[0]

    m = build(configure)
    try:
        groups = (
            m.params,
            m.param_quantities,
            m.inputs,
            m.input_infos,
            m.outputs,
            m.output_infos,
            m.lights,
            m.light_infos,
        )
        quantity = m.get_param_quantity(2)
        observed = {
            "sizes": [len(group) for group in groups],
            "names": [m.get_input_info(0).get_name(), m.get_output_info(0).get_name(), quantity.get_label()],
            "params": [p.value for p in m.params],
            "null_lights": [m.get_light_info(i) is None for i in range(3)],
        }
        assert observed == reference["raw_vectors"]
        assert m.get_num_params() == 3 and m.get_num_inputs() == 1 and m.get_num_lights() == 3
    finally:
        m.close()


@pytest.mark.parametrize(
    ("edit", "message"),
    [
        (lambda m: m.params.append(Param()), "3 params but 2 parameter quantities"),
        (lambda m: m.param_quantities.__setitem__(0, None), "parameter 0 without a quantity"),
        (lambda m: m.input_infos.__setitem__(1, None), "input 1 without metadata"),
        (lambda m: m.inputs.__delitem__(0), "different sizes"),
        (lambda m: m.param_quantities.__setitem__(1, ParamQuantity()), "parameter quantity 1 not set"),
        (lambda m: (m.inputs.__delitem__(0), m.input_infos.__delitem__(0)), "port metadata 0"),
        (lambda m: setattr(m.input_infos[0], "type", Port.Type.OUTPUT), "port metadata 0"),
        (lambda m: m.light_infos.__setitem__(0, LightInfo()), "light metadata 0"),
        (lambda m: m.lights.append(Light()), "2 lights but 1 light metadata slots"),
        (
            lambda m: (m.config_bypass(1, 0), m.inputs.resize(1), m.input_infos.resize(1)),
            "bypass route outside",
        ),
    ],
)
def test_seal_rejects_what_rack_would_crash_on(edit: Callable[[Module], object], message: str) -> None:
    escaped: list[Module] = []
    baseline = _rackpy_host.transferred_objects()

    def configure(m: Module) -> None:
        escaped.append(m)
        edit(m)

    with pytest.raises(ValueError, match=message):
        build(configure)
    assert not escaped[0].alive
    assert _rackpy_host.transferred_objects() == baseline


def test_valid_light_and_quantity_slots_pass_the_seal() -> None:
    light, quantity = LightInfo(), SwitchQuantity()

    def configure(m: Module) -> None:
        light.module, light.light_id, light.name = m, 0, "Raw light"
        m.light_infos[0] = light
        quantity.module, quantity.param_id, quantity.name = m, 1, "Raw switch"
        m.param_quantities[1] = quantity

    m = build(configure)
    try:
        assert m.get_light_info(0) is light and m.get_param_quantity(1) is quantity
        assert light.get_name() == "Raw light" and quantity.get_label() == "Raw switch"
    finally:
        m.close()
    assert not light.alive and not quantity.alive  # the module deleted them, as Rack's destructor does


def test_slot_assignment_transfers_and_raw_removal_releases() -> None:
    kept: dict[str, Any] = {}
    baseline = _rackpy_host.transferred_objects()

    def configure(m: Module) -> None:
        builtin = m.get_input_info(0)
        first = port_info(m, 0, Port.Type.INPUT, "First")
        m.input_infos[0] = first
        assert m.input_infos[0] is first and first.alive and not builtin.alive
        assert _rackpy_host.transferred_objects() == baseline + 1
        second = port_info(m, 0, Port.Type.INPUT, "Second")
        m.input_infos[0] = second
        # A raw edit drops the pointer without deleting the object: usable again.
        assert first.alive and first.get_name() == "First" and first.module is m
        first.name = "Standalone again"
        m.input_infos.insert(-1, None)  # clamped like list.insert
        slot: object = m.input_infos[1]
        assert len(m.input_infos) == 3 and slot is None
        del m.input_infos[1]
        m.light_infos.append(None)
        m.lights.append(Light())
        m.light_infos.resize(1)
        m.lights.resize(1)
        kept.update(first=first, second=second)

    m = build(configure)
    first, second = kept["first"], kept["second"]
    try:
        assert m.get_input_info(0) is second and second.alive
        assert _rackpy_host.transferred_objects() == baseline + 1
    finally:
        m.close()
    # The released object kept a tracked link: deleting the module cleared it.
    assert alive(first) and module_of(first) is None and first.name == "Standalone again"
    assert not alive(second)
    assert _rackpy_host.transferred_objects() == baseline


def test_slot_value_errors_change_nothing() -> None:
    owned_elsewhere: list[PortInfo] = []

    def take(m: Module) -> None:
        owned_elsewhere.append(port_info(m, 0, Port.Type.INPUT))
        m.input_infos[0] = owned_elsewhere[0]

    holder = build(take, (0, 1, 0, 0))

    def configure(m: Module) -> None:
        view = m.get_input_info(0)
        bad: list[Any] = [view, LightInfo(), ParamQuantity(), object(), "PortInfo"]
        for value in bad:
            with pytest.raises(TypeError, match="takes a PortInfo"):
                m.input_infos[0] = value
        with pytest.raises(TypeError, match="takes a ParamQuantity"):
            m.param_quantities[0] = PortInfo()  # type: ignore[assignment]
        with pytest.raises(TypeError, match="takes a LightInfo"):
            m.light_infos[0] = PortInfo()  # type: ignore[assignment]
        with pytest.raises(TypeError, match="takes Param values"):
            m.params[0] = Input()  # type: ignore[assignment]
        with pytest.raises(TypeError, match="takes Input values"):
            m.inputs.append(Output())  # type: ignore[arg-type]
        with pytest.raises(OwnershipError, match="already owns"):
            m.input_infos[0] = owned_elsewhere[0]
        for i in (-2, 1):
            with pytest.raises(IndexError):
                m.input_infos[i] = None
            with pytest.raises(IndexError):
                del m.inputs[i]
        with pytest.raises(ValueError, match="negative size"):
            m.params.resize(-1)
        assert m.get_input_info(0) is view and view.alive and len(m.inputs) == 1

    m = build(configure, (1, 1, 0, 1))
    retired = owned_elsewhere[0]
    holder.close()
    try:

        def reuse(m: Module) -> None:
            with pytest.raises(StaleHandleError, match="already released"):
                m.input_infos[0] = retired

        build(reuse, (0, 1, 0, 0)).close()
    finally:
        m.close()


def test_component_edits_copy_values_and_invalidate_only_that_vector() -> None:
    def configure(m: Module) -> None:
        param, light, inp = m.params[0], m.lights[0], m.inputs[0]
        source = Param()
        source.value = 0.75
        m.params[1] = source  # copy assignment: views stay valid
        assert m.params[1].value == 0.75 and param.alive
        source.value = 0.1
        assert m.params[1].value == 0.75  # a copy, not an alias
        incoming = Input()
        incoming.set_voltage(1.5)
        m.inputs[0] = incoming
        assert inp.alive and inp.get_voltage() == 1.5
        m.params.append(Param())
        m.param_quantities.append(None)
        m.config_param(2, 0, 1, 0)
        # A structural edit invalidates that vector's views, like C++ iterators.
        assert not alive(param) and alive(light) and alive(inp)
        with pytest.raises(StaleHandleError):
            _ = param.value
        assert m.params[0].alive and m.params[0] is m.params[0]

    m = build(configure)
    try:
        # After the seal, element writes remain; structure and slots do not.
        replacement = Light()
        replacement.set_brightness(1)
        m.lights[0] = replacement
        assert m.lights[0].get_brightness() == 1
        edits: list[Callable[[], object]] = [
            lambda: m.params.append(Param()),
            lambda: m.params.resize(3),
            m.params.clear,
            lambda: m.inputs.__delitem__(0),
            lambda: m.lights.insert(0, Light()),
            lambda: m.param_quantities.__setitem__(0, None),
            lambda: m.light_infos.__setitem__(0, None),
        ]
        for edit in edits:
            with pytest.raises(OwnershipError, match="constructor"):
                edit()
        assert len(m.params) == 3 and len(m.inputs) == 2
    finally:
        m.close()


def test_edits_need_config_and_the_main_thread() -> None:
    def before_config(m: Module) -> None:
        with pytest.raises(OwnershipError, match=r"Module\.config"):
            m.params.append(Param())
        m.config(1, 0, 0, 0)
        actions: list[Callable[[], object]] = [
            lambda: m.params.append(Param()),
            lambda: m.params.__setitem__(0, Param()),
            lambda: m.param_quantities.__setitem__(0, None),
            lambda: m.param_quantities.resize(2),
        ]
        errors: list[BaseException] = []

        def worker() -> None:
            for action in actions:
                try:
                    action()
                except BaseException as error:
                    errors.append(error)

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()
        assert len(errors) == len(actions) and all(isinstance(e, ThreadError) for e in errors)
        assert len(m.params) == 1 and len(m.param_quantities) == 1

    Module.create(before_config).close()


def test_identity_fields_are_constructor_scoped() -> None:
    held: dict[str, Any] = {}

    def configure(m: Module) -> None:
        view = m.get_input_info(0)
        assert isinstance(view, PortInfoView)
        view.port_id = 1
        view.type = Port.Type.OUTPUT
        view.module = None
        assert view.module is None and view.port_id == 1
        with pytest.raises(ValueError, match="only at that module"):
            view.module = held["other"]
        view.module, view.port_id, view.type = m, 0, Port.Type.INPUT
        quantity = m.get_param_quantity(0)
        assert isinstance(quantity, ParamQuantityView)
        quantity.param_id = 1
        assert quantity.get_param() is m.params[1]
        quantity.param_id = 7  # out of range: Rack's getParam returns null
        assert quantity.get_param() is None
        quantity.param_id = 0
        light = LightInfo()
        light.module, light.light_id = m, 0
        m.light_infos[0] = light
        light.light_id = 0  # still editable during construction
        held.update(light=light, view=view)

    held["other"] = Module.create()
    m = build(configure)
    try:
        light, view = held["light"], held["view"]
        assert isinstance(m.get_light_info(0), LightInfo)
        frozen: list[Callable[[], object]] = [
            lambda: setattr(light, "light_id", 1),
            lambda: setattr(light, "module", None),
            lambda: setattr(view, "port_id", 1),
            lambda: setattr(view, "module", None),
        ]
        for edit in frozen:
            with pytest.raises(OwnershipError, match=r"construction|constructor"):
                edit()
        light.name = "Still editable"  # not an identity field
        assert light.get_name() == "Still editable" and light.light_id == 0 and light.module is m
        assert isinstance(m.get_light_info(0), LightInfo) and not isinstance(m.get_light_info(0), LightInfoView)
    finally:
        m.close()
        held["other"].close()


def test_rollback_retires_transferred_objects_and_releases_nothing_twice() -> None:
    made: list[PortInfo] = []
    baseline = _rackpy_host.transferred_objects()

    def failing(m: Module) -> None:
        made.append(port_info(m, 0, Port.Type.INPUT))
        m.input_infos[0] = made[0]
        m.input_infos.append(port_info(m, 2, Port.Type.INPUT))
        m.inputs.append(Input())
        added = m.input_infos[2]
        assert isinstance(added, PortInfo)
        made.append(added)
        raise RuntimeError("raw rollback")

    with pytest.raises(RuntimeError, match="raw rollback"):
        build(failing)
    assert len(made) == 2 and not any(info.alive for info in made)
    assert _rackpy_host.transferred_objects() == baseline


def test_one_thousand_raw_edits_and_owner_churns() -> None:
    baseline = _rackpy_host.transferred_objects()
    tracked: weakref.WeakSet[PortInfo] = weakref.WeakSet()
    owners: weakref.WeakSet[Module] = weakref.WeakSet()

    def configure(m: Module) -> None:
        for i in range(1000):
            info = port_info(m, 1, Port.Type.OUTPUT, str(i))
            tracked.add(info)
            m.outputs.append(Output())
            m.output_infos.append(info)
            del m.outputs[1]
            del m.output_infos[1]  # released, then collected when `info` is rebound
        assert len(m.outputs) == len(m.output_infos) == 1
        assert _rackpy_host.transferred_objects() == baseline

    m = build(configure)
    del configure
    gc.collect()
    assert not tracked
    m.close()

    for i in range(1000):

        def edit(m: Module, odd: bool = bool(i % 2)) -> None:
            if odd:
                m.light_infos[0] = None
            else:
                m.lights.resize(1)

        module = build(edit)
        owners.add(module)
        del module
    gc.collect()
    assert not owners
    assert _rackpy_host.transferred_objects() == baseline
