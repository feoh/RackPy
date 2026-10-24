# SPDX-License-Identifier: GPL-3.0-or-later
"""RP-15C: the json_t boundary as Rack reaches it (live UI suite only).

Rack writes a patch with json_dumpf and reads it with json_loadf, without
JSON_ALLOW_NUL. These tests go through that text, reach the boundary with
JSON that Python values cannot express, and count jansson's allocations and
references around every conversion. The production start-up run ignores this
file: it needs the test hooks and Rack's engine.
"""

import functools
import json
import math
from collections.abc import Callable
from typing import Any

import _rackpy_host
import pytest
from _rackpy_host import _testing as HOOKS

from rackpy.raw.engine import Module, ParamQuantity

DEPTH = _rackpy_host.JSON_MAX_DEPTH


def nested(levels: int) -> Any:
    value: Any = []
    for _ in range(levels):
        value = [value]
    return value


def adopted(build: Callable[[Module], None]) -> Module:
    m = Module.create(build)
    HOOKS.engine_adopt(m)
    return m


def raised() -> int:
    return _rackpy_host.dispatch_stats()["raised"]


def recorded(after: int) -> list[str]:
    return [message for _, _, _, _, message in _rackpy_host.diagnostics(after)]


def no_jansson_leak(action: Callable[[], object]) -> None:
    assert HOOKS.json_allocations(action) == 0


def test_the_allocation_probe_sees_a_leak() -> None:
    assert HOOKS.json_allocations(HOOKS.leak_json_integer) == 1
    assert HOOKS.json_allocations(lambda: None) == 0


def test_converted_values_hold_one_reference_and_borrowed_ones_keep_theirs() -> None:
    value = {"a": [1, "s", {"b": 2.5, "c": [True, None]}], "d": -(2**63)}
    counts = HOOKS.json_refcounts(value)
    assert len(counts) == 8 and set(counts) == {1}
    before, after, converted = HOOKS.json_borrowed_refcounts(json.dumps(value))
    assert before == after and set(before) == {1}
    assert converted == value


def test_rack_cannot_load_nul_so_the_boundary_refuses_it() -> None:
    ok, message = HOOKS.rack_parses_json_text('{"s": "a\\u0000b"}')
    assert not ok and "\\u0000" in message
    m = Module.create(lambda m: m.set_data_handlers(to_json=lambda: {"s": "a\x00b"}))
    try:
        with pytest.raises(ValueError, match="NUL"):
            m.data_to_json()
        with pytest.raises(ValueError, match="NUL"):
            m.data_from_json(["a\x00b"])
    finally:
        m.close()


def test_custom_data_survives_rack_patch_text() -> None:
    loaded: list[Any] = []
    data: dict[str, Any] = {
        "ints": [-(2**63), 2**63 - 1, 0, -1],
        "floats": [-0.0, 0.1, 5e-324, 1.7976931348623157e308, 3.0],
        "text": 'λ 𝄞 "quoted" \\ \n\t\x01 \u2028',
        "": None,
        "flags": [True, False],
        "deepest": nested(DEPTH - 1),  # "data" itself is the first level
    }

    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 4)
        m.set_data_handlers(to_json=lambda: data, from_json=loaded.append)

    saved = adopted(build)
    try:
        saved.params[0].value = 7
        text = HOOKS.engine_module_to_json_text(saved)
    finally:
        HOOKS.engine_delete(saved)
    assert HOOKS.rack_parses_json_text(text) == (True, "")
    reloaded = adopted(build)
    try:
        HOOKS.engine_module_from_json_text(reloaded, text)
        assert loaded == [data] and reloaded.params[0].value == 7
        assert math.copysign(1, loaded[0]["floats"][0]) == -1
        assert all(type(f) is float for f in loaded[0]["floats"])
    finally:
        HOOKS.engine_delete(reloaded)


def test_patch_data_python_could_not_build_is_contained() -> None:
    loaded: list[Any] = []

    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 4)
        m.set_data_handlers(from_json=loaded.append)

    m = adopted(build)
    try:
        patch = json.loads(HOOKS.engine_module_to_json_text(m))
        patch["params"][0]["value"] = 6
        patch["data"] = nested(DEPTH + 10)  # jansson parses up to 2048 levels
        count, sequence = raised(), _rackpy_host.diagnostics_recorded()
        HOOKS.engine_module_from_json_text(m, json.dumps(patch))
        assert loaded == [] and m.params[0].value == 6  # the rest of Rack's load happened
        assert raised() == count + 1
        assert any("Module.data_from_json" in text and "nested deeper" in text for text in recorded(sequence))

        patch["params"][0]["value"] = 3
        del patch["data"]
        HOOKS.engine_module_from_json_text(m, json.dumps(patch), invalid_utf8_data=b"bad \xff\xfe")
        assert loaded == [] and m.params[0].value == 3
        assert raised() == count + 2
        assert any("Module.data_from_json" in text and "utf-8" in text.lower() for text in recorded(sequence))
    finally:
        HOOKS.engine_delete(m)


def test_malformed_module_json_follows_rack() -> None:
    def build(m: Module) -> None:
        m.config(2, 0, 0, 0)
        m.config_param(0, 0, 10, 1)
        m.config_param(1, 0, 10, 2)

    m = adopted(build)
    try:
        patch = json.loads(HOOKS.engine_module_to_json_text(m))
        patch["params"] = [5, {"id": "x", "value": "y"}, {"id": -1, "value": 9}, {"id": 1, "value": 8}]
        HOOKS.engine_module_from_json_text(m, json.dumps(patch))
        # Rack: a non-object falls back to its index (0) with no value; a string ID
        # reads as 0 and a string value as 0; -1 is out of range; ID 1 is set.
        assert [p.value for p in m.params] == [0, 8]
        patch["params"] = "not a list"
        HOOKS.engine_module_from_json_text(m, json.dumps(patch))
        assert [p.value for p in m.params] == [0, 8]
        with pytest.raises(ValueError, match="Rack would refuse"):
            HOOKS.engine_module_from_json_text(m, '{"params": [')
    finally:
        HOOKS.engine_delete(m)


def test_quantity_json_overrides_must_return_a_dict() -> None:
    class Listed(ParamQuantity):
        def to_json(self) -> Any:
            return [self.get_value()]

    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 4, quantity_type=Listed)

    plain = Module.create(build)
    try:
        with pytest.raises(TypeError, match="must return a dict"):
            plain.params_to_json()
    finally:
        plain.close()
    m = adopted(build)
    try:
        count = raised()
        assert HOOKS.engine_module_to_json(m)["params"] == [{"value": 4, "id": 0}]  # Rack's own JSON
        assert raised() == count + 1
    finally:
        HOOKS.engine_delete(m)


def test_conversions_and_their_failures_leak_no_jansson_memory() -> None:
    value: dict[str, Any] = {"ok": [1, 2.5, "s", {"x": None}]}

    def build(m: Module) -> None:
        m.config(1, 0, 0, 0)
        m.config_param(0, 0, 10, 4)
        m.set_data_handlers(to_json=lambda: value, from_json=lambda data: None)

    def raises(error: type[Exception], call: Callable[[], object]) -> Callable[[], None]:
        def action() -> None:
            with pytest.raises(error):
                call()

        return action

    # Refusals part-way through a structure: what was built must be released.
    for data, error in [
        ([1, "s", {"a": [2, math.nan]}], ValueError),
        ({"a": 1, "b": {2: 3}}, TypeError),
        ({"a": [1, "\ud800"]}, UnicodeError),
        ([{"a": 2**64}], OverflowError),
        ([1, nested(DEPTH + 1)], ValueError),
        ({"a": ["x", "b\x00"]}, ValueError),
    ]:
        no_jansson_leak(raises(error, functools.partial(_rackpy_host.json_round_trip, data)))
    no_jansson_leak(lambda: _rackpy_host.json_round_trip(value))

    m = Module.create(build)
    try:
        no_jansson_leak(m.data_to_json)
        no_jansson_leak(lambda: m.data_from_json(value))
        no_jansson_leak(m.params_to_json)
        no_jansson_leak(lambda: m.params_from_json([{"id": 0, "value": 5}]))
        value["bad"] = [1, math.inf]
        no_jansson_leak(raises(ValueError, m.data_to_json))
        no_jansson_leak(raises(ValueError, lambda: m.data_from_json(nested(DEPTH + 1))))
    finally:
        m.close()

    # Rack's own save and load around contained failures.
    del value["bad"]
    m = adopted(build)
    try:
        no_jansson_leak(lambda: HOOKS.engine_module_to_json(m))
        value["bad"] = {"deep": [1, math.nan]}
        count = raised()
        no_jansson_leak(lambda: HOOKS.engine_module_to_json(m))
        assert raised() == count + 1
        text = HOOKS.engine_module_to_json_text(m)
        patch = json.loads(text)
        patch["data"] = nested(DEPTH + 10)
        no_jansson_leak(lambda: HOOKS.engine_module_from_json_text(m, json.dumps(patch)))
        no_jansson_leak(lambda: HOOKS.engine_module_from_json_text(m, text, invalid_utf8_data=b"\xff"))
    finally:
        HOOKS.engine_delete(m)
