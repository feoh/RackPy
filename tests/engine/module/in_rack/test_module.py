# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Rack construction, checked access, rollback and native bypass semantics."""

import gc
import threading
import weakref
from typing import Any

import pytest

from rackpy.engine import Module as FacadeModule
from rackpy.errors import OwnershipError, StaleHandleError, ThreadError
from rackpy.raw.engine import PORT_MAX_CHANNELS, Module


def test_construction_matches_native_reference(reference: dict[str, Any]) -> None:
    assert FacadeModule is Module
    empty = Module.create()
    assert not list(empty.params)
    empty.close()
    for case in reference["cases"]:
        n = case["n"]

        def configure(m: Module, n: int = n) -> None:
            m.config(n, n, n, n)

        m = Module.create(configure)
        assert [m.get_num_params(), m.get_num_inputs(), m.get_num_outputs(), m.get_num_lights()] == case["counts"]
        assert [p.get_value() for p in m.params] == case["params"]
        assert [p.get_channels() for p in m.inputs] == case["inputs"]
        assert [p.get_channels() for p in m.outputs] == case["outputs"]
        assert [p.get_brightness() for p in m.lights] == case["lights"]
        m.close()
        assert not m.alive
        m.close()  # idempotent


def test_component_views_share_identity_and_write_through() -> None:
    m = Module.create(lambda m: m.config(2, 2, 2, 2))
    for group, getter in (
        (m.params, m.get_param),
        (m.inputs, m.get_input),
        (m.outputs, m.get_output),
        (m.lights, m.get_light),
    ):
        assert len(group) == 2
        assert group[0] is getter(0)
        assert group[-1] is getter(1)
        assert list(group)[1] is getter(1)
        for bad in (-3, 2):
            with pytest.raises(IndexError):
                _ = group[bad]
        for bad in (-1, 2):
            with pytest.raises(IndexError):
                getter(bad)
    m.params[0].value = 0.75
    m.inputs[1].voltage = 3.5
    m.outputs[0].voltage = -2
    m.lights[1].brightness = 0.25
    assert m.get_param(0).get_value() == 0.75
    assert m.get_input(1).get_voltage() == 3.5
    assert m.get_output(0).get_voltage() == -2
    assert m.get_light(1).get_brightness() == 0.25
    with pytest.raises(AttributeError):
        m.params = []  # type: ignore[misc,assignment]
    m.close()


@pytest.mark.parametrize("n", [0, 1])
def test_config_once_and_constructor_phase(n: int) -> None:
    def configure(m: Module) -> None:
        m.config(n, n, n)
        with pytest.raises(OwnershipError, match="only once"):
            m.config(0, 0, 0)
        with pytest.raises(OwnershipError, match="during"):
            m.close()

    m = Module.create(configure)
    with pytest.raises(OwnershipError, match="constructor"):
        m.config(0, 0, 0)
    with pytest.raises(OwnershipError, match="constructor"):
        m.config_bypass(0, 0)
    m.close()


def test_invalid_counts_and_ids_are_checked_before_rack() -> None:
    def configure(m: Module) -> None:
        for counts in ((-1, 0, 0, 0), (0, -1, 0, 0), (0, 0, -1, 0), (0, 0, 0, -1)):
            with pytest.raises(ValueError, match="nonnegative"):
                m.config(*counts)
        # Validation errors did not partially configure the native module.
        m.config(0, 2, 2)
        for ids in ((-1, 0), (0, -1), (2, 0), (0, 2)):
            with pytest.raises(IndexError):
                m.config_bypass(*ids)
        m.config_bypass(0, 1)
        with pytest.raises(ValueError, match="already"):
            m.config_bypass(1, 1)
        m.config_bypass(0, 0)  # one input can route to multiple outputs

    m = Module.create(configure)
    assert [(r.input_id, r.output_id) for r in m.bypass_routes] == [(0, 1), (0, 0)]
    route = Module.BypassRoute()
    assert (route.input_id, route.output_id) == (-1, -1)
    route.input_id, route.output_id = 1, 0
    assert (route.input_id, route.output_id) == (1, 0)
    copied = m.bypass_routes[0]
    copied.output_id = 99
    assert m.bypass_routes[0].output_id == 1  # reading copies: it cannot corrupt routing
    with pytest.raises(OwnershipError, match="constructor"):
        m.bypass_routes[0] = copied  # route edits are constructor-only (RP-14C4)
    m.close()
    assert copied.output_id == 99  # value survives
    with pytest.raises(TypeError, match="callable"):
        Module.create(123)  # type: ignore[call-overload]


def test_close_invalidates_all_retained_views() -> None:
    m = Module.create(lambda m: m.config(1, 1, 1, 1))
    groups = m.params, m.inputs, m.outputs, m.lights
    p, inp, out, light = groups[0][0], groups[1][0], groups[2][0], groups[3][0]
    voltages = out.voltages
    m.close()
    for access in (
        m.get_num_params,
        lambda: m.params,
        lambda: m.bypass_routes,
        lambda: m.get_param(0),
        lambda: len(groups[0]),
        lambda: groups[1][0],
        p.get_value,
        inp.get_voltage,
        out.get_voltage,
        light.get_brightness,
        lambda: voltages[0],
    ):
        with pytest.raises(StaleHandleError):
            access()
    assert not p.alive and not inp.alive and not out.alive and not light.alive


def test_failed_constructor_invalidates_even_escaped_handles() -> None:
    escaped: list[Any] = []

    class ConstructionFailure(Exception):
        pass

    def configure(m: Module) -> None:
        m.config(1, 1, 1, 1)
        escaped.extend((m, m.params, m.get_param(0), m.get_output(0).voltages))
        raise ConstructionFailure("partial configuration")

    with pytest.raises(ConstructionFailure, match="partial"):
        Module.create(configure)
    assert not escaped[0].alive
    for access in (escaped[0].get_num_params, lambda: len(escaped[1]), escaped[2].get_value, lambda: escaped[3][0]):
        with pytest.raises(StaleHandleError):
            access()
    escaped[0].close()


def test_collections_do_not_extend_module_lifetime_and_churn_is_bounded() -> None:
    seen: weakref.WeakSet[Module] = weakref.WeakSet()
    for _ in range(1000):
        m = Module.create(lambda m: m.config(1, 1, 1, 1))
        seen.add(m)
        group, p = m.params, m.get_param(0)
        del m
        with pytest.raises(StaleHandleError):
            p.get_value()
        with pytest.raises(StaleHandleError):
            len(group)
    gc.collect()
    assert len(seen) == 0


def test_nested_constructors_are_independent_transactions() -> None:
    escaped: list[Module] = []

    def configure(outer: Module) -> None:
        outer.config(1, 0, 0)
        inner = Module.create(lambda m: m.config(2, 0, 0))
        escaped.append(inner)
        with pytest.raises(OwnershipError, match="constructor"):
            inner.config(0, 0, 0)
        assert len(outer.params) == 1 and len(inner.params) == 2

    m = Module.create(configure)
    assert len(m.params) == 1
    m.close()
    assert len(escaped[0].params) == 2
    escaped[0].close()


def test_main_thread_rule() -> None:
    m = Module.create(lambda m: m.config(1, 1, 1, 1))
    group, component = m.params, m.get_param(0)
    outcomes: list[BaseException] = []

    def worker() -> None:
        for action in (Module.create, m.close, m.get_num_params, lambda: len(group), component.get_value):
            try:
                action()
            except BaseException as e:
                outcomes.append(e)

    t = threading.Thread(target=worker)
    t.start()
    t.join()
    assert len(outcomes) == 5 and all(isinstance(e, ThreadError) for e in outcomes)
    assert m.alive
    m.close()


def test_bypass_routes_match_native_reference(reference: dict[str, Any]) -> None:
    try:
        from _rackpy_host import _testing
    except ImportError:
        pytest.skip("native dispatch hook is deliberately absent from the production build")
    for case in reference["bypass"]:

        def configure(m: Module) -> None:
            m.config(0, 1, 1)
            m.config_bypass(0, 0)

        m = Module.create(configure)
        n = case["input_channels"]
        m.inputs[0].channels = n  # native fixture setup: simulate cable connection
        m.outputs[0].channels = 1
        for i in range(n):
            m.inputs[0].set_voltage(i * 0.25 - 2, i)
        _testing.module_process_bypass(m)
        assert m.outputs[0].channel_count == case["output_channels"]
        assert m.outputs[0].voltages.tolist() == case["voltages"]
        assert len(m.outputs[0].voltages) == PORT_MAX_CHANNELS
        m.close()


def test_documented_construction_example() -> None:
    def wire(module: Module) -> None:
        module.config(num_params=1, num_inputs=1, num_outputs=1, num_lights=1)
        module.config_bypass(input_id=0, output_id=0)

    module = Module.create(wire)
    try:
        module.params[0].value = 0.5
        assert module.get_param(0) is module.params[0]
    finally:
        module.close()
