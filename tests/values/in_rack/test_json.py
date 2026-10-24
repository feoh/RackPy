# SPDX-License-Identifier: GPL-3.0-or-later
"""The json_t policy inside Rack (native/runtime/json.cpp), through jansson and back."""

import math

import _rackpy_host
import pytest

round_trip = _rackpy_host.json_round_trip


def test_values_round_trip() -> None:
    value = {"a": [1, 2.5, "s", None, True, False, {"b": []}], "unicode": "Ünïcødé ✓", "": -(2**63)}
    assert round_trip(value) == value
    assert round_trip((1, 2)) == [1, 2]  # tuples become arrays
    assert round_trip(2**63 - 1) == 2**63 - 1


@pytest.mark.parametrize("bad", [math.nan, math.inf, -math.inf, [1, math.nan]])
def test_nonfinite_floats_are_refused(bad: object) -> None:
    with pytest.raises(ValueError, match="NaN or infinity"):
        round_trip(bad)


def test_integers_outside_json_int_are_refused() -> None:
    with pytest.raises(OverflowError, match="json_int_t"):
        round_trip(2**63)


def test_keys_types_and_depth_are_checked() -> None:
    with pytest.raises(TypeError, match="keys must be str"):
        round_trip({1: "x"})
    with pytest.raises(ValueError, match="NUL"):
        round_trip({"a\x00b": 1})
    with pytest.raises(ValueError, match="NUL"):
        round_trip(["nul\x00inside"])  # Rack loads JSON without JSON_ALLOW_NUL (RP-15C)
    with pytest.raises(TypeError, match="cannot become JSON"):
        round_trip({"x": {1, 2}})
    deep: list[object] = []
    nested = deep
    for _ in range(_rackpy_host.JSON_MAX_DEPTH + 1):
        child: list[object] = []
        nested.append(child)
        nested = child
    with pytest.raises(ValueError, match="nested deeper"):
        round_trip(deep)
    cycle: list[object] = []
    cycle.append(cycle)
    with pytest.raises(ValueError, match="containing itself"):
        round_trip(cycle)
