# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.dsp's triggers, pulse generator, timers and clock divider inside Rack (RP-18B).

The reference (native/tests/dsp_reference.cpp) runs Rack's objects over whole
input streams, recording each result and state; each stream replays here on
a fresh object. The other tests check construction, enums, views, the
deprecated union fields and refusals.
"""

import copy
import math
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.dsp.digital
from rackpy.raw import dsp
from rackpy.raw.simd import float_4, int32_4


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


def signed(bits: str) -> int:
    value: int = struct.unpack("<i", struct.pack("<I", int(bits, 16)))[0]
    return value


def decode(value: Any) -> Any:
    if isinstance(value, dict):
        if "f" in value:
            return f32(value["f"])
        if "f4" in value:
            return float_4.cast(int32_4(*map(signed, value["f4"])))
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


def same(actual: Any, expected: Any) -> bool:
    if isinstance(expected, float):
        if math.isnan(expected):
            return isinstance(actual, float) and math.isnan(actual)
        return isinstance(actual, float) and actual == expected
    if isinstance(expected, float_4):
        return isinstance(actual, float_4) and int32_4.cast(actual).tolist() == int32_4.cast(expected).tolist()
    if isinstance(expected, list):
        return (
            isinstance(actual, list | tuple)
            and len(actual) == len(expected)
            and all(same(a, e) for a, e in zip(actual, expected, strict=True))
        )
    return type(actual) is type(expected) and bool(actual == expected)


def boolean(event: bool) -> Callable[[list[bool]], list[list[Any]]]:
    def run(inputs: list[bool]) -> list[list[Any]]:
        t = dsp.BooleanTrigger()
        if event:
            return [[t.process_event(v).value, t.s.value] for v in inputs]
        return [[t.process(v), t.s.value] for v in inputs]

    return run


def schmitt(event: bool) -> Callable[..., list[list[Any]]]:
    def run(inputs: list[float], low: float, high: float) -> list[list[Any]]:
        t = dsp.SchmittTrigger()
        if event:
            return [[t.process_event(x, low, high).value, t.s.value] for x in inputs]
        return [[t.process(x, low, high), t.s.value] for x in inputs]

    return run


def schmitt4(inputs: list[float_4], low: float, high: float) -> list[list[float_4]]:
    t = dsp.TSchmittTrigger_float_4()
    out = []
    for x in inputs:
        r = t.process(x, low, high)
        out.append([r, float_4(t.state), t.is_high()])  # state is a live view: copy it
    return out


def pulse(script: list[float]) -> list[list[Any]]:
    p = dsp.PulseGenerator()
    out = []
    for op in script:
        if op >= 0.0:
            p.trigger(op)
            out.append([p.is_high(), p.remaining])
        else:
            out.append([p.process(-op), p.remaining])
    return out


def default_trigger() -> float:
    p = dsp.PulseGenerator()
    p.trigger()
    return p.remaining


def timer(deltas: list[Any]) -> list[list[Any]]:
    t: Any = dsp.TTimer[float_4]() if deltas and isinstance(deltas[0], float_4) else dsp.Timer()
    return [[t.process(dt), t.get_time()] for dt in deltas]


def divider(division: int) -> list[list[Any]]:
    c = dsp.ClockDivider()
    c.set_division(division)
    out = []
    for _ in range(12):
        ticked = c.process()
        out.append([ticked, c.get_clock()])
    return out


CALLS: dict[str, Callable[..., Any]] = {
    "boolean_trigger.process": boolean(False),
    "boolean_trigger.process_event": boolean(True),
    "schmitt.process": schmitt(False),
    "schmitt.process_event": schmitt(True),
    "schmitt4.process": schmitt4,
    "pulse.script": pulse,
    "pulse.default_trigger": default_trigger,
    "timer.process": timer,
    "timer4.process": timer,
    "clock_divider.process": divider,
}


def test_every_reference_stream_matches_rack(digital_cases: list[dict[str, Any]]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    for case in digital_cases:
        fn = case["fn"]
        seen[fn] += 1
        expected = decode(case["result"])
        actual = CALLS[fn](*decode(case["args"]))
        if not same(actual, expected):
            mismatches.append(f"{fn}{case['args']!r}: Rack gives {expected!r}, rackpy.raw gives {actual!r}")
    assert not mismatches, f"{len(mismatches)} streams differ:\n" + "\n".join(m[:2000] for m in mismatches[:5])
    assert set(seen) == set(CALLS), "every reference stream is replayed, and nothing else"


def test_boolean_trigger() -> None:
    t = dsp.BooleanTrigger()
    assert t.s is dsp.BooleanTrigger.State.UNINITIALIZED and not t.is_high()
    assert not t.process(True)  # from UNINITIALIZED, a first true input does not trigger
    assert t.is_high() and not t.process(True) and not t.process(False) and t.process(True)
    assert t.process_event(False) is dsp.BooleanTrigger.Event.UNTRIGGERED
    assert t.process_event(True) is dsp.BooleanTrigger.Event.TRIGGERED
    assert t.process_event(True) is dsp.BooleanTrigger.Event.NONE
    assert dsp.BooleanTrigger.Event.UNTRIGGERED.value == -1
    t.reset()
    assert t.s is dsp.BooleanTrigger.State.UNINITIALIZED
    t.s = dsp.BooleanTrigger.State.LOW
    assert t.process(True)
    # The deprecated bool union member, read and written through s.
    with pytest.warns(DeprecationWarning, match="state"):
        assert t.state is True
    t.reset()
    with pytest.warns(DeprecationWarning):
        assert t.state is True  # UNINITIALIZED reads as true, as its byte does
    with pytest.warns(DeprecationWarning):
        t.state = False
    assert t.s is dsp.BooleanTrigger.State.LOW and repr(t) == "BooleanTrigger(s=LOW)"
    with pytest.raises(TypeError):
        hash(t)


def test_schmitt_trigger_for_floats() -> None:
    t = dsp.SchmittTrigger()
    assert t.s is dsp.SchmittTrigger.State.UNINITIALIZED
    assert not t.process(1.0)  # UNINITIALIZED to HIGH without triggering
    assert not t.process(0.5) and t.is_high()  # between the thresholds: no change
    assert not t.process(0.0) and not t.is_high()
    assert t.process(1.0) and t.is_high()
    assert not t.process(math.nan) and t.is_high()  # NaN reaches neither threshold
    assert t.process_event(-1.0) is dsp.SchmittTrigger.Event.UNTRIGGERED
    assert t.process_event(2.0, 0.1, 2.0) is dsp.SchmittTrigger.Event.TRIGGERED
    with pytest.warns(DeprecationWarning):
        assert t.state is True
    other = copy.copy(t)
    other.reset()
    states: tuple[object, object] = (t.s, other.s)  # (mypy would keep t.s narrowed from the first assert)
    assert states == (dsp.SchmittTrigger.State.HIGH, dsp.SchmittTrigger.State.UNINITIALIZED)
    enums: tuple[object, object] = (dsp.SchmittTrigger.State, dsp.BooleanTrigger.State)
    assert enums[0] is not enums[1]  # two enums, as in C++


def test_schmitt_trigger_for_vectors() -> None:
    t = dsp.TSchmittTrigger_float_4()
    assert int32_4.cast(t.state).tolist() == [-1] * 4  # Rack's generic template resets to a full mask
    triggered = t.process(float_4(1.0, 0.5, 0.0, 2.0))
    assert int32_4.cast(triggered).tolist() == [0, 0, 0, 0]  # already HIGH
    t.process(float_4(0.0))  # all LOW
    triggered = t.process(float_4(1.0, 0.5, 0.0, 2.0), 0.0, 1.0)
    assert int32_4.cast(triggered).tolist() == [-1, 0, 0, -1]
    view = t.state
    view[1] = float_4.cast(int32_4(-1))[0]  # the state is a live view: set lane 1 HIGH
    assert int32_4.cast(t.is_high()).tolist() == [-1, -1, 0, -1]
    t.reset()
    assert int32_4.cast(t.state).tolist() == [-1] * 4


def test_templates_and_aliases() -> None:
    assert dsp.TSchmittTrigger[float] is dsp.SchmittTrigger is dsp.TSchmittTrigger[()]
    assert dsp.TSchmittTrigger[float_4] is dsp.TSchmittTrigger_float_4
    assert dsp.TTimer[float] is dsp.Timer is dsp.TTimer[()] and dsp.TTimer[float_4] is dsp.TTimer_float_4
    for template in (dsp.TSchmittTrigger, dsp.TTimer):
        with pytest.raises(TypeError, match="not defined in Rack"):
            template[int]
        with pytest.raises(TypeError, match="template"):
            template()
    assert rackpy.dsp.digital.SchmittTrigger is dsp.SchmittTrigger


def test_pulse_generator() -> None:
    p = dsp.PulseGenerator()
    assert not p.is_high() and not p.process(0.1)
    p.trigger()
    assert p.remaining == struct.unpack("<f", struct.pack("<f", 1e-3))[0] and p.is_high()
    p.trigger(1e-4)  # shorter than what remains: kept
    assert p.remaining == struct.unpack("<f", struct.pack("<f", 1e-3))[0]
    assert p.process(2e-3) and not p.is_high()  # HIGH before the step
    assert not p.process(1e-3)
    p.trigger(0.5)
    p.reset()
    assert p.remaining == 0.0


def test_timers() -> None:
    t = dsp.Timer()
    assert t.process(0.25) == 0.25 and t.process(0.5) == 0.75 and t.get_time() == 0.75
    t.reset()
    assert t.time == 0.0
    v = dsp.TTimer_float_4()
    assert v.process(float_4(1, 2, 3, 4)).tolist() == [1.0, 2.0, 3.0, 4.0]
    assert v.process(0.5).tolist() == [1.5, 2.5, 3.5, 4.5]  # a scalar broadcasts
    v.time[0] = 0.0  # a live view
    assert v.get_time().tolist() == [0.0, 2.5, 3.5, 4.5]


def test_clock_divider() -> None:
    c = dsp.ClockDivider()
    assert c.get_division() == 1 and all(c.process() for _ in range(3))
    c.set_division(3)
    assert [c.process() for _ in range(6)] == [False, False, True, False, False, True]
    c.set_division(0)
    assert c.process() and c.get_clock() == 0  # clock >= 0 always holds
    c.clock = 2**32 - 1
    c.set_division(2**32 - 1)
    assert not c.process() and c.clock == 0  # the count wraps, as unsigned arithmetic does
    for bad in (-1, 2**32):
        with pytest.raises(OverflowError):
            c.set_division(bad)
        with pytest.raises(OverflowError):
            c.clock = bad
    with pytest.raises(TypeError):
        c.set_division(1.5)  # type: ignore[arg-type]
    assert repr(dsp.ClockDivider()) == "ClockDivider(clock=0, division=1)"
