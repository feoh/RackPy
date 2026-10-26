# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.dsp's helpers inside Rack (RP-18A), against Rack's compiled code.

native/tests/dsp_reference.cpp runs Rack's common, approx, convert, window and
ode code over edge cases and seeded random inputs; every case replays here,
bit for bit. The other tests check refusals, buffers, templates, the ODE
callbacks and Frame.
"""

import array
import copy
import ctypes
import math
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.dsp.basic
from rackpy.raw import dsp
from rackpy.raw.simd import float_4, int32_4


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


def signed(bits: str) -> int:
    value: int = struct.unpack("<i", struct.pack("<I", int(bits, 16)))[0]
    return value


def decode(value: Any) -> Any:
    """The reference's encoding as Python values; vectors from bit patterns (Rack's FTZ would flush floats)."""
    if isinstance(value, dict):
        if "f" in value:
            return f32(value["f"])
        if "f4" in value:
            return float_4.cast(int32_4(*map(signed, value["f4"])))
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


def subnormal_scalar(value: Any) -> bool:
    if isinstance(value, dict) and "f" in value:
        return 0 < int(value["f"], 16) & 0x7FFFFFFF < 0x00800000
    if isinstance(value, list):
        return any(map(subnormal_scalar, value))
    return False


# polyDirect and polyEstrin: the reference calls the same out-of-line instantiations as the bindings
# (native/bindings/dsp/poly.hpp), because Rack's flags let the compiler reassociate their sums
# differently at each call site. So every case, these included, matches exactly.
def same_bits(actual: int, expected: int) -> bool:
    """Float lanes as patterns: any NaN equals any NaN, and ±0 are equal (not Rack semantics, ADR 009)."""
    actual &= 0xFFFFFFFF
    expected &= 0xFFFFFFFF
    if expected & 0x7F800000 == 0x7F800000 and expected & 0x007FFFFF:
        return actual & 0x7F800000 == 0x7F800000 and bool(actual & 0x007FFFFF)
    if expected & 0x7FFFFFFF == 0:
        return actual & 0x7FFFFFFF == 0
    return actual == expected


def same(actual: Any, expected: Any) -> bool:
    if isinstance(expected, float):
        if not isinstance(actual, float):
            return False
        if 0 < abs(expected) < 1.1754943508222875e-38:
            return actual in (0.0, expected)  # reading a subnormal C++ float into Python flushes it
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


def rounded(x: float) -> float:
    value: float = struct.unpack("<f", struct.pack("<f", x))[0]
    return value


def window(fn: Callable[..., None]) -> Callable[..., list[float]]:
    def run(*args: Any) -> list[float]:
        *head, values, length = args
        x = array.array("f", values)
        fn(*head, x, length)
        return x.tolist()

    return run


def oscillator(t: Any, x: Any, dxdt: Any) -> None:
    dxdt[0] = x[1]
    dxdt[1] = -x[0]


def decay(t: float, x: Any, dxdt: Any) -> None:
    for k in range(3):
        dxdt[k] = -0.5 * x[k] + t


STEPPERS = {"euler": dsp.step_euler, "rk2": dsp.step_rk2, "rk4": dsp.step_rk4}


def ode(method: str, system: Callable[..., None]) -> Callable[..., list[float]]:
    def run(start: list[float], dt: float, steps: int) -> list[float]:
        x = array.array("f", start)
        t = 0.0
        for _ in range(steps):
            STEPPERS[method](t, dt, x, len(start), system)
            t = rounded(t + dt)  # C++ accumulates t in float
        return x.tolist()

    return run


def ode4(method: str) -> Callable[..., list[float_4]]:
    def run(start: list[float_4], dt: float, steps: int) -> list[float_4]:
        x = list(start)
        t, step = float_4(0.0), float_4(dt)
        for _ in range(steps):
            STEPPERS[method](t, step, x, 2, oscillator)
            t = t + step
        return x

    return run


def int24_bytes(raw: bytearray) -> list[int]:
    return [int.from_bytes(raw[k : k + 3], "little", signed=True) for k in range(0, len(raw), 3)]


def convert_buffer(to: Any, frm: Any, code_in: str, code_out: str, count: int) -> Callable[[list[Any]], list[Any]]:
    def run(values: list[Any]) -> list[Any]:
        src = array.array(code_in, values)
        if to is dsp.Int24:
            out = bytearray(3 * len(values))
            dsp.convert[to, frm](src, out, len(values))
            return int24_bytes(out)
        dst = array.array(code_out, [0] * len(values))
        dsp.convert[to, frm](src, dst, len(values))
        return dst.tolist()

    return run


C = ctypes
CALLS: dict[str, Callable[..., Any]] = {
    "FREQ_C4": lambda: dsp.FREQ_C4,
    "FREQ_A4": lambda: dsp.FREQ_A4,
    "FREQ_SEMITONE": lambda: dsp.FREQ_SEMITONE,
    "exponential_bipolar": dsp.exponential_bipolar,
    "exponential_bipolar_4": dsp.exponential_bipolar,
    "exp2_floor": dsp.exp2_floor,
    "exp2_floor_4": dsp.exp2_floor,
    "exp2_taylor5": dsp.exp2_taylor5,
    "exp2_taylor5_4": dsp.exp2_taylor5,
    "blackman": dsp.blackman,
    "blackman_4": dsp.blackman,
    "convert.float.float": dsp.convert[float, float],
    "convert.float.int8": dsp.convert[float, C.c_int8],
    "convert.float.int16": dsp.convert[float, C.c_int16],
    "convert.float.int32": dsp.convert[float, C.c_int32],
    "convert.float.int64": dsp.convert[float, C.c_int64],
    "convert.float.int24": lambda v: dsp.convert[float, dsp.Int24](dsp.Int24(v)),
    "convert.int8.float": dsp.convert[C.c_int8, float],
    "convert.int16.float": dsp.convert[C.c_int16, float],
    "convert.int24.float": lambda x: dsp.convert[dsp.Int24, float](x).i,
    "convert.int32.float": dsp.convert[C.c_int32, float],
    "convert.int64.float": dsp.convert[C.c_int64, float],
    "int24": lambda v: dsp.Int24(v).i,
    "convert_buffer.int16.float": convert_buffer(C.c_int16, float, "f", "h", 6),
    "convert_buffer.float.int16": convert_buffer(float, C.c_int16, "h", "f", 4),
    "convert_buffer.int24.float": convert_buffer(dsp.Int24, float, "f", "B", 3),
    "hann_window": window(dsp.hann_window),
    "blackman_nuttall_window": window(dsp.blackman_nuttall_window),
    "blackman_harris_window": window(dsp.blackman_harris_window),
    "blackman_window": window(dsp.blackman_window),
}
TEMPLATES = """sinc amplitude_to_db db_to_amplitude quadratic_bipolar cubic quartic_bipolar quintic sqrt_bipolar
hann blackman_nuttall blackman_harris poly_direct poly_horner poly_estrin"""
for _name in TEMPLATES.split():
    CALLS[_name] = CALLS[_name + "_4"] = getattr(dsp, _name)
for _method in STEPPERS:
    CALLS[f"ode.{_method}.oscillator"] = ode(_method, oscillator)
    CALLS[f"ode.{_method}.decay"] = ode(_method, decay)
    CALLS[f"ode4.{_method}.oscillator"] = ode4(_method)


def test_every_reference_case_matches_rack(reference: list[dict[str, Any]]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    skipped = 0
    for case in reference:
        fn = case["fn"]
        seen[fn] += 1
        if subnormal_scalar(case["args"]):
            skipped += 1  # a Python float cannot reach Rack as a subnormal C++ float
            continue
        args = decode(case["args"])
        expected = decode(case["result"])
        actual = CALLS[fn](*args)
        if not same(actual, expected):
            mismatches.append(f"{fn}{tuple(args)!r}: Rack gives {expected!r}, rackpy.raw gives {actual!r}")
    assert not mismatches, f"{len(mismatches)} of {len(reference)} cases differ:\n" + "\n".join(mismatches[:40])
    assert set(seen) == set(CALLS), "every reference function is replayed, and nothing else"
    assert len(reference) > 4000 and skipped < len(reference) // 50


def test_templates_take_floats_and_vectors() -> None:
    assert isinstance(dsp.cubic(2.0), float) and dsp.cubic(2.0) == 8.0
    v = dsp.cubic(float_4(1, 2, 3, 4))
    assert isinstance(v, float_4) and v.tolist() == [1.0, 8.0, 27.0, 64.0]
    assert dsp.exponential_bipolar(float_4(2.0), 1.0).tolist() == [dsp.exponential_bipolar(2.0, 1.0)] * 4
    assert dsp.sinc(0.0) == 1.0 and dsp.sinc(float_4(0.0)).tolist() == [1.0] * 4
    assert dsp.db_to_amplitude(20.0) == pytest.approx(10.0) and dsp.amplitude_to_db(10.0) == pytest.approx(20.0)
    assert math.isnan(dsp.sqrt_bipolar(-4.0))  # sgn(x) * sqrt(x): Rack takes the root of the signed value
    assert dsp.FREQ_A4 == 440.0 and rounded(261.6256) == dsp.FREQ_C4
    assert dsp.hann(0.5) == 1.0 and dsp.blackman(0.16, 0.0) == pytest.approx(0.0, abs=1e-6)
    assert rackpy.dsp.basic.cubic is dsp.cubic and rackpy.dsp.basic.Frame is dsp.Frame


def test_polynomials_and_their_coefficient_counts() -> None:
    a = [1.0, 2.0, 3.0]  # 1 + 2x + 3x**2
    for fn in (dsp.poly_direct, dsp.poly_horner, dsp.poly_estrin):
        assert fn(a, 2.0) == 17.0
        assert fn(a, float_4(0, 1, 2, -1)).tolist() == [1.0, 6.0, 17.0, 2.0]  # coefficients broadcast
        assert fn([float_4(1, 0, 0, 0), float_4(0, 1, 0, 0)], 3.0).tolist() == [1.0, 3.0, 0.0, 0.0]
        assert fn([0.5] * 32, 1.0) == 16.0
        with pytest.raises(ValueError, match="1 to 32"):
            fn([], 1.0)
        with pytest.raises(ValueError, match="1 to 32"):
            fn([1.0] * 33, 1.0)


def test_exp2_floor_refuses_racks_undefined_inputs() -> None:
    assert dsp.exp2_floor(3.25) == (8.0, 0.25) and dsp.exp2_floor(0.0) == (1.0, 0.0)
    assert dsp.exp2_taylor5(3.0) == 8.0 and dsp.exp2_taylor5(0.5) == pytest.approx(math.sqrt(2), rel=6e-6)
    for bad in (-128.5, 129.0, 1e9, math.inf, -math.inf, math.nan):
        with pytest.raises(ValueError, match="undefined"):
            dsp.exp2_floor(bad)
        with pytest.raises(ValueError, match="undefined"):
            dsp.exp2_taylor5(bad)
    # The float_4 specialization uses SSE conversions, defined for every lane.
    y, frac = dsp.exp2_floor(float_4(3.25, 1e9, math.nan, -1000.0))
    assert y[0] == 8.0 and frac[0] == 0.25
    # Rack's deprecated aliases still work, and warn.
    with pytest.warns(DeprecationWarning, match="exp2_floor"):
        assert dsp.approx_exp2_floor(3.25) == (8.0, 0.25)
    with pytest.warns(DeprecationWarning, match="exp2_taylor5"):
        assert dsp.approx_exp2_taylor5(float_4(3.0)).tolist() == [8.0] * 4
    with pytest.warns(DeprecationWarning), pytest.raises(ValueError):
        dsp.approx_exp2_taylor5(500.0)


def test_int24() -> None:
    assert dsp.Int24().i == 0 and int(dsp.Int24(-5)) == -5 and repr(dsp.Int24(7)) == "Int24(7)"
    assert dsp.Int24(2**23).i == -(2**23) and dsp.Int24(0x12345678).i == 0x345678  # the low 24 bits
    n = dsp.Int24(1)
    n.i = -1
    assert n.i == -1
    with pytest.raises(OverflowError):
        dsp.Int24(2**31)
    c = copy.copy(n)
    c.i = 3
    assert n.i == -1
    with pytest.raises(TypeError):
        hash(n)


def test_convert_template() -> None:
    assert dsp.convert[float, C.c_int16](16384) == 0.5
    assert dsp.convert[C.c_float, C.c_int16] is dsp.convert[float, C.c_int16]
    assert dsp.convert[C.c_int, float] is dsp.convert[C.c_int32, float]  # the same ctypes class on Linux
    assert dsp.convert[C.c_int16, float](0.5) == 16384 and dsp.convert[C.c_int8, float](2.0) == 127  # clamped above
    # Rack clamps only above: below range the C++ narrowing wraps (an int8_t of -128000 is 0).
    assert dsp.convert[C.c_int8, float](-1000.0) == 0
    assert dsp.convert[float, dsp.Int24](4194304) == 0.5  # an int converts to Int24, as in C++
    with pytest.raises(OverflowError):
        dsp.convert[float, C.c_int8](128)
    with pytest.raises(TypeError, match="Python int"):
        dsp.convert[float, C.c_int16](1.5)
    for undefined in ((C.c_int8, C.c_int16), (float, int), (C.c_uint8, float), (float,)):
        with pytest.raises(TypeError, match="not defined in Rack"):
            dsp.convert[undefined]
    with pytest.raises(TypeError, match="template"):
        dsp.convert(1.0)
    # Buffers: the item types must match, and out must be writable and long enough.
    out = array.array("h", [0] * 4)
    dsp.convert[C.c_int16, float](array.array("f", [0.5, -0.5, 1.0, 0.0]), out, 3)
    assert out.tolist() == [16384, -16384, 32767, 0]  # only len samples
    with pytest.raises(TypeError, match="int16_t"):
        dsp.convert[C.c_int16, float](array.array("f", [0.0]), array.array("i", [0]), 1)
    with pytest.raises(ValueError, match="shorter"):
        dsp.convert[C.c_int16, float](array.array("f", [0.0] * 4), out, 5)
    with pytest.raises(BufferError):
        dsp.convert[float, float](array.array("f", [1.0]), memoryview(array.array("f", [0.0])).toreadonly(), 1)
    # Overlapping arrays of different types would be undefined in Rack's loop; the same type is a copy.
    shared = array.array("i", [1, 2, 3, 4])
    with pytest.raises(ValueError, match="overlap"):
        dsp.convert[float, C.c_int32](shared, memoryview(shared).cast("B").cast("f"), 4)
    same = array.array("f", [1.0, 2.0])
    dsp.convert[float, float](same, same, 2)
    assert same.tolist() == [1.0, 2.0]


def test_windows_apply_in_place_and_check_lengths() -> None:
    x = array.array("f", [1.0] * 5)
    dsp.hann_window(x, 5)
    assert x.tolist() == [dsp.hann(k / 4) for k in range(5)]
    y = array.array("f", [2.0] * 4)
    dsp.blackman_window(0.16, y, 0)  # len 0: nothing to do
    assert y.tolist() == [2.0] * 4
    with pytest.raises(ValueError, match="len is 9"):
        dsp.hann_window(x, 9)
    with pytest.raises(TypeError, match="float32"):
        dsp.hann_window(array.array("d", [1.0]), 1)
    with pytest.raises(BufferError):
        dsp.blackman_harris_window(memoryview(x).toreadonly(), 1)


def test_ode_steppers_call_python_on_copies() -> None:
    seen: list[Any] = []

    def f(t: float, x: Any, dxdt: Any) -> None:
        seen.append((t, x))
        assert x.readonly and isinstance(dxdt, array.array)
        with pytest.raises(TypeError):
            x[0] = 1.0  # x is const in C++
        dxdt[0] = 1.0

    x = array.array("f", [0.0, 5.0])
    dsp.step_rk4(0.0, 0.5, x, 2, f)
    assert len(seen) == 4 and x.tolist() == [0.5, 5.0]  # dxdt[1] was left 0: RackPy zeroes it
    assert [t for t, _ in seen] == [0.0, 0.25, 0.25, 0.5]
    assert seen[0][1].tolist() == [0.0, 5.0]  # a copy of x at the call, which may outlive it safely

    # An exception from f leaves x untouched and reaches the caller.
    def broken(t: float, x: Any, dxdt: Any) -> None:
        raise RuntimeError("from f")

    with pytest.raises(RuntimeError, match="from f"):
        dsp.step_euler(0.0, 0.1, x, 2, broken)
    assert x.tolist() == [0.5, 5.0]
    for bad in (0, -1, 3, 5000):
        with pytest.raises(ValueError, match="len"):
            dsp.step_euler(0.0, 0.1, x, bad, f)
    # float_4: x is a list of vectors, updated in place.
    v = [float_4(1.0), float_4(0.0)]
    first = v[0]
    dsp.step_euler(float_4(0.0), 0.5, v, 2, oscillator)
    assert first is v[0] and first.tolist() == [1.0] * 4 and v[1].tolist() == [-0.5] * 4


def test_frame_template() -> None:
    mono: type[dsp.Frame] = dsp.Frame[16]
    assert mono is dsp.Frame[16, float] and mono.CHANNELS == 16 and mono.type is float
    frame = mono()
    assert isinstance(frame, dsp.Frame) and frame.samples.tolist() == [0.0] * 16
    frame.samples[3] = 2.5
    assert memoryview(frame).tolist()[3] == 2.5 and memoryview(frame).format == "f"
    vectors = dsp.Frame[2, float_4]()
    vectors.samples[1] = float_4(1, 2, 3, 4)
    vectors.samples[0] = 5.0  # a scalar broadcasts
    assert vectors.samples[1].tolist() == [1.0, 2.0, 3.0, 4.0] and len(vectors.samples) == 2
    assert memoryview(vectors).tolist() == [5.0] * 4 + [1.0, 2.0, 3.0, 4.0]  # Rack's layout, lane by lane
    with pytest.raises(IndexError):
        vectors.samples[2]
    other = copy.copy(frame)
    other.samples[3] = 0.0
    assert frame.samples[3] == 2.5 and repr(dsp.Frame[1]()) == "Frame[1, float]([0.0])"
    with pytest.raises(TypeError, match="template"):
        dsp.Frame()
    with pytest.raises(ValueError, match="at least one"):
        dsp.Frame[0]
    for bad in ((2, int), ("2",), (2, float, 3), (True,)):
        with pytest.raises(TypeError):
            dsp.Frame[bad]
    with pytest.raises(TypeError, match="already"):
        mono[4]
    with pytest.raises(TypeError):
        hash(frame)
