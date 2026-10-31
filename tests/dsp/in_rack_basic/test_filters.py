# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.dsp's filters inside Rack (RP-18C): RC, exponential, peak, slew, IIR and biquad.

The reference (native/tests/dsp_reference.cpp) runs each filter, for float
and float_4, over a whole stream on a fresh object, recording outputs, state,
coefficients and frequency responses; each replays here identically. The
other tests check construction, views, the templates, the deprecated forms,
refusals and the facade's bulk path.
"""

import array
import copy
import math
import struct
import warnings
from collections import Counter
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.dsp.filters
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


def same_bits(actual: int, expected: int) -> bool:
    actual &= 0xFFFFFFFF
    expected &= 0xFFFFFFFF
    if expected & 0x7F800000 == 0x7F800000 and expected & 0x007FFFFF:
        return actual & 0x7F800000 == 0x7F800000 and bool(actual & 0x007FFFFF)
    if expected & 0x7FFFFFFF == 0:
        return actual & 0x7FFFFFFF == 0
    return actual == expected


def same(actual: Any, expected: Any) -> bool:
    """As ADR 009: NaN payloads and the sign of zero are not Rack semantics; subnormals read as zero."""
    if isinstance(expected, float):
        if not isinstance(actual, float):
            return False
        if 0 < abs(expected) < 1.1754943508222875e-38:
            return actual in (0.0, expected)
        if math.isnan(expected):
            return math.isnan(actual)
        return actual == expected
    if isinstance(expected, float_4):
        lanes = zip(int32_4.cast(actual).tolist(), int32_4.cast(expected).tolist(), strict=True)
        return isinstance(actual, float_4) and all(same_bits(a, e) for a, e in lanes)
    if isinstance(expected, list):
        return (
            isinstance(actual, list | tuple)
            and len(actual) == len(expected)
            and all(same(a, e) for a, e in zip(actual, expected, strict=True))
        )
    return type(actual) is type(expected) and bool(actual == expected)


def first_difference(actual: Any, expected: Any, path: str = "") -> str:
    """Where two results first differ, as an index path, with both values there."""
    if isinstance(expected, list) and isinstance(actual, list | tuple) and len(actual) == len(expected):
        for k, (a, e) in enumerate(zip(actual, expected, strict=True)):
            if not same(a, e):
                return first_difference(a, e, f"{path}[{k}]")
    return f"at {path or 'the top'}: Rack gives {expected!r}, rackpy.raw gives {actual!r}"


def copied(x: Any) -> Any:
    return float_4(x) if isinstance(x, float_4) else x


def rc(vector: bool) -> Callable[..., list[list[Any]]]:
    def run(inputs: list[Any], fc: float) -> list[list[Any]]:
        f = dsp.TRCFilter[float_4]() if vector else dsp.RCFilter()
        f.set_cutoff_freq(fc)
        out = []
        for x in inputs:
            f.process(x)
            out.append([f.lowpass(), f.highpass(), copied(f.c)])
        return out

    return run


def smoothers(vector: bool) -> Callable[..., list[list[Any]]]:
    def run(
        inputs: list[Any], lam: float, tau: float, rise: float, fall: float, rise_tau: float, fall_tau: float, dt: float
    ) -> Any:
        t: Any = float_4 if vector else float
        e = dsp.TExponentialFilter[t]()
        p = dsp.TPeakFilter[t]()
        s = dsp.TSlewLimiter[t]()
        x = dsp.TExponentialSlewLimiter[t]()
        e.set_lambda(lam)
        p.set_tau(tau)
        s.set_rise_fall(rise, fall)
        x.set_rise_fall_tau(rise_tau, fall_tau)
        rows = [[e.process(dt, v), p.process(dt, v), s.process(dt, v), x.process(dt, v)] for v in inputs]
        return [rows, copied(x.rise_lambda), copied(x.fall_lambda)]

    return run


def deprecated_smoothers(vector: bool) -> Callable[..., list[list[Any]]]:
    def run(inputs: list[Any]) -> list[list[Any]]:
        t: Any = float_4 if vector else float
        e, p, s, x = (
            dsp.TExponentialFilter[t](),
            dsp.TPeakFilter[t](),
            dsp.TSlewLimiter[t](),
            dsp.TExponentialSlewLimiter[t](),
        )
        e.set_lambda(0.25)
        s.set_rise_fall(0.5, 0.25)
        x.set_rise_fall(0.5, 0.125)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", DeprecationWarning)
            p.set_rate(0.75)
            out = []
            for v in inputs:
                row = [e.process(v), p.process(v), s.process(v), x.process(v)]
                out.append([*row, copied(p.peak())])
        return out

    return run


def iir(vector: bool, b_order: int, a_order: int) -> Callable[..., list[Any]]:
    def run(b: list[Any], a: list[Any], inputs: list[Any], frequencies: list[list[float]]) -> list[Any]:
        f = dsp.IIRFilter[b_order, a_order, float_4 if vector else float]()
        f.set_coefficients(b, a)
        out = [f.process(x) for x in inputs]
        response = []
        for fr, angular in frequencies:
            h = f.get_transfer_function(angular)
            real, imag = (h.real, h.imag) if isinstance(h, complex) else h
            response.append([real, imag, f.get_frequency_response(fr), f.get_frequency_phase(fr)])
        return [out, response]

    return run


def biquad(vector: bool) -> Callable[..., list[Any]]:
    def run(kind: str, freq: float, q: float, v: float, frequencies: list[float]) -> list[Any]:
        f = dsp.TBiquadFilter[float_4]() if vector else dsp.BiquadFilter()
        types = dsp.TBiquadFilter_float_4.Type if vector else dsp.BiquadFilter.Type
        f.set_parameters(getattr(types, kind), freq, q, v)
        coefficients = [*f.b.tolist(), *f.a.tolist()]
        impulse = [f.process(1.0 if k == 0 else 0.0) for k in range(16)]
        return [coefficients, impulse, [f.get_frequency_response(fr) for fr in frequencies]]

    return run


def biquad_types(vector: bool) -> Callable[[], list[list[Any]]]:
    """Every enumerator of the Python enum by name and value, in order, as Rack's header declares them."""

    def run() -> list[list[Any]]:
        types = dsp.TBiquadFilter_float_4.Type if vector else dsp.BiquadFilter.Type
        return [[t.name, t.value] for t in types]

    return run


def biquad_default(vector: bool) -> Callable[[], list[Any]]:
    def run() -> list[Any]:
        f = dsp.TBiquadFilter[float_4]() if vector else dsp.BiquadFilter()
        return [copied(f.b[0]), copied(f.a[0]), f.process(1.0)]

    return run


CALLS: dict[str, Callable[..., Any]] = {
    "rc": rc(False),
    "rc4": rc(True),
    "smoothers": smoothers(False),
    "smoothers4": smoothers(True),
    "deprecated_smoothers": deprecated_smoothers(False),
    "deprecated_smoothers4": deprecated_smoothers(True),
    "biquad": biquad(False),
    "biquad4": biquad(True),
    "biquad_default": biquad_default(False),
    "biquad_default4": biquad_default(True),
    "biquad_types": biquad_types(False),
    "biquad_types4": biquad_types(True),
}
for _b, _a in ((2, 2), (3, 3), (4, 2), (2, 5), (8, 8)):
    CALLS[f"iir.{_b}.{_a}"] = iir(False, _b, _a)
for _b, _a in ((3, 3), (5, 4)):
    CALLS[f"iir4.{_b}.{_a}"] = iir(True, _b, _a)


def test_every_reference_stream_matches_rack(filter_cases: list[dict[str, Any]]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    for case in filter_cases:
        fn = case["fn"]
        seen[fn] += 1
        expected = decode(case["result"])
        actual = CALLS[fn](*decode(case["args"]))
        if not same(actual, expected):
            mismatches.append(f"{fn} (args {str(case['args'])[:200]}...): {first_difference(actual, expected)}")
    assert not mismatches, f"{len(mismatches)} streams differ:\n" + "\n".join(mismatches[:5])
    assert set(seen) == set(CALLS), "every reference stream is replayed, and nothing else"


def test_templates_and_aliases() -> None:
    pairs = (
        (dsp.TRCFilter, dsp.RCFilter, dsp.TRCFilter_float_4),
        (dsp.TExponentialFilter, dsp.ExponentialFilter, dsp.TExponentialFilter_float_4),
        (dsp.TPeakFilter, dsp.PeakFilter, dsp.TPeakFilter_float_4),
        (dsp.TSlewLimiter, dsp.SlewLimiter, dsp.TSlewLimiter_float_4),
        (dsp.TExponentialSlewLimiter, dsp.ExponentialSlewLimiter, dsp.TExponentialSlewLimiter_float_4),
        (dsp.TBiquadFilter, dsp.BiquadFilter, dsp.TBiquadFilter_float_4),
    )
    for template, scalar, vector in pairs:
        assert template[float] is scalar is template[()] and template[float_4] is vector
        with pytest.raises(TypeError, match="not defined in Rack"):
            template[int]
    assert dsp.IIRFilter[3, 3] is dsp.IIRFilter[3, 3, float] is dsp.IIRFilter_3_3_float
    assert dsp.IIRFilter[3, 3, float_4] is dsp.IIRFilter_3_3_float_4
    assert issubclass(dsp.BiquadFilter, dsp.IIRFilter_3_3_float)
    assert dsp.IIRFilter[2, 8].B_ORDER == 2 and dsp.IIRFilter[2, 8].A_ORDER == 8
    for bad in ((1, 3), (3, 1), (9, 2), (2, 9), (3, 3, int)):
        with pytest.raises(TypeError, match="not defined in Rack"):
            dsp.IIRFilter[bad]
    assert dsp.TBiquadFilter_float_4.Type is dsp.BiquadFilter.Type
    assert rackpy.dsp.filters.BiquadFilter is dsp.BiquadFilter


def test_single_pole_filters() -> None:
    e = dsp.ExponentialFilter()
    e.set_tau(0.5)
    assert e.lambda_ == 2.0
    assert e.process(0.25, 1.0) == 0.5 and e.out == 0.5
    e.lambda_ = 0.0
    assert e.process(1.0, 3.0) == 3.0  # no change in out: Rack snaps to the input
    p = dsp.PeakFilter()
    p.set_lambda(1.0)
    assert p.process(0.5, 4.0) == 4.0 and p.process(0.5, 0.0) == 2.0  # jumps up, decays
    s = dsp.SlewLimiter()
    s.set_rise_fall(10.0, 1.0)
    assert s.process(0.1, 5.0) == 1.0 and s.process(0.1, -5.0) == pytest.approx(0.9)
    x = dsp.ExponentialSlewLimiter()
    x.set_rise_fall(2.0, 0.0)
    assert (x.rise_lambda, x.fall_lambda) == (2.0, 0.0)
    assert x.process(0.25, 1.0) == 0.5 and x.process(1.0, 0.0) == 0.0  # fall lambda 0: snaps
    x.set_rise_fall_tau(0.5, 0.25)
    assert (x.rise_lambda, x.fall_lambda) == (2.0, 4.0)  # lambda = 1 / tau
    x.rise_lambda = 1.0  # the fields are what process reads
    x.reset()
    assert x.process(0.5, 1.0) == 0.5
    rcf = dsp.RCFilter()
    rcf.set_cutoff(2.0)
    assert rcf.c == 1.0
    rcf.process(1.0)
    assert rcf.lowpass() == 0.5 and rcf.highpass() == 0.5
    assert rcf.xstate.tolist() == [1.0] and rcf.ystate[0] == 0.5
    rcf.ystate[0] = 0.0  # a live view
    assert rcf.lowpass() == 0.0
    with pytest.raises(IndexError):
        rcf.xstate[1]
    other = copy.copy(rcf)
    other.reset()
    assert rcf.xstate[0] == 1.0 and other.xstate[0] == 0.0
    # The vector forms broadcast scalars, and their fields are live views.
    v = dsp.TSlewLimiter_float_4()
    v.set_rise_fall(float_4(1, 2, 3, 4), 1.0)
    assert v.process(1.0, 10.0).tolist() == [1.0, 2.0, 3.0, 4.0]
    v.out[0] = 0.0
    assert v.out.tolist() == [0.0, 2.0, 3.0, 4.0]


def test_deprecated_forms_warn() -> None:
    e = dsp.ExponentialFilter()
    e.set_lambda(0.5)
    with pytest.warns(DeprecationWarning, match="process"):
        assert e.process(2.0) == 1.0  # process(1, in)
    p = dsp.PeakFilter()
    with pytest.warns(DeprecationWarning, match="set_rate"):
        p.set_rate(0.25)
    assert p.lambda_ == 0.75
    with pytest.warns(DeprecationWarning, match="peak"):
        assert p.peak() == 0.0
    for cls in (dsp.PeakFilter, dsp.SlewLimiter, dsp.ExponentialSlewLimiter, dsp.TSlewLimiter_float_4):
        with pytest.warns(DeprecationWarning):
            cls().process(1.0)


def test_iir_filters() -> None:
    f = dsp.IIRFilter[3, 2]()
    assert f.b.tolist() == [0.0] * 3 and len(f.a) == 1 and len(f.x) == 2 and len(f.y) == 1
    f.set_coefficients([0.5, 0.25, 0.0], [-0.5])  # y = 0.5 x + 0.25 x1 + 0.5 y1
    assert [f.process(v) for v in (1.0, 0.0, 0.0)] == [0.5, 0.5, 0.25]
    assert f.x.tolist() == [0.0, 0.0] and f.y.tolist() == [0.25]
    f.reset()
    assert f.y.tolist() == [0.0] and f.b.tolist() == [0.5, 0.25, 0.0]  # reset keeps the coefficients
    h = f.get_transfer_function(0.0)
    assert isinstance(h, complex) and h.real == pytest.approx(1.5)  # (0.5 + 0.25) / (1 - 0.5)
    assert f.get_frequency_response(0.0) == pytest.approx(1.5) and f.get_frequency_phase(0.0) == pytest.approx(0.0)
    with pytest.raises(ValueError, match="3 b and 1 a"):
        f.set_coefficients([1.0, 2.0], [0.0])
    with pytest.raises(ValueError, match="3 b and 1 a"):
        f.set_coefficients([1.0, 2.0, 3.0], [])
    v = dsp.IIRFilter[3, 3, float_4]()
    v.set_coefficients([1.0, 0.0, 0.0], [0.0, 0.0])
    re, im = v.get_transfer_function(0.5)
    assert re.tolist() == [1.0] * 4 and im.tolist() == [0.0] * 4
    v.b[0] = float_4(1, 2, 3, 4)  # an item of a vector array is a live view too
    v.b[0][3] = 8.0
    assert v.process(1.0).tolist() == [1.0, 2.0, 3.0, 8.0]


def test_biquad_filters() -> None:
    fresh = dsp.BiquadFilter()
    assert math.isnan(fresh.b[0]) and math.isnan(fresh.process(1.0))  # Rack's constructor divides 0 by 0
    lp = dsp.BiquadFilter()
    lp.set_parameters(dsp.BiquadFilter.Type.LOWPASS, 0.1, 0.707, 1.0)
    assert lp.get_frequency_response(0.0001) == pytest.approx(1.0, abs=1e-3)
    assert lp.get_frequency_response(0.45) < 0.05
    nothing = dsp.BiquadFilter()
    nothing.set_parameters(dsp.BiquadFilter.Type.NUM_TYPES, 0.1, 1.0, 1.0)  # Rack's switch does nothing
    assert math.isnan(nothing.b[0])
    v = dsp.TBiquadFilter_float_4()
    v.set_parameters(dsp.BiquadFilter.Type.HIGHPASS, 0.1, 0.707, 1.0)
    assert v.get_frequency_response(0.45)[0] == pytest.approx(lp.get_frequency_response(0.0001), abs=0.05)
    with pytest.raises(TypeError):
        v.set_parameters(2, 0.1, 0.7, 1.0)  # type: ignore[arg-type]  # the enum, not an int


def test_bulk_processing_equals_per_call_processing() -> None:
    samples = array.array("f", [math.sin(k * 0.37) * 3 for k in range(500)])
    timed: tuple[tuple[Callable[[], Any], float | None], ...] = (
        (dsp.ExponentialFilter, 1e-3),
        (dsp.PeakFilter, 1e-3),
        (dsp.SlewLimiter, 1e-3),
        (dsp.ExponentialSlewLimiter, 1e-3),
        (dsp.BiquadFilter, None),
        (dsp.IIRFilter[4, 3], None),
    )
    for make, dt in timed:
        one: Any = make()
        bulk: Any = make()
        for f in (one, bulk):
            if isinstance(f, dsp.BiquadFilter):
                f.set_parameters(dsp.BiquadFilter.Type.PEAK, 0.05, 2.0, 3.0)
            elif hasattr(f, "set_lambda"):
                f.set_lambda(20.0)
            elif hasattr(f, "set_rise_fall"):
                f.set_rise_fall(100.0, 30.0)
            else:
                f.set_coefficients([0.2, 0.3, 0.1, 0.05], [-0.3, 0.1])
        expected = [one.process(x) for x in samples] if dt is None else [one.process(dt, x) for x in samples]
        out = array.array("f", [0.0] * len(samples))
        if dt is None:
            rackpy.dsp.filters.process_block(bulk, samples, out)
        else:
            rackpy.dsp.filters.process_block(bulk, samples, out, dt)
        assert out.tolist() == expected
        in_place = array.array("f", samples)
        fresh: Any = make()
        if isinstance(fresh, dsp.BiquadFilter):
            fresh.set_parameters(dsp.BiquadFilter.Type.PEAK, 0.05, 2.0, 3.0)
            rackpy.dsp.filters.process_block(fresh, in_place, in_place)
            assert in_place.tolist() == expected  # in place: each sample is read before it is written
    with pytest.raises(ValueError, match="shorter"):
        rackpy.dsp.filters.process_block(dsp.BiquadFilter(), samples, array.array("f", [0.0]))
    with pytest.raises(TypeError, match="delta_time"):
        rackpy.dsp.filters.process_block(dsp.SlewLimiter(), samples, array.array("f", samples))  # type: ignore[call-overload]  # also refused at runtime
    with pytest.raises(TypeError):
        rackpy.dsp.filters.process_block(dsp.RCFilter(), samples, array.array("f", samples))  # type: ignore[call-overload]  # also refused at runtime
