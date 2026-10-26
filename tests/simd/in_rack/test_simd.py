# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.simd and rackpy.raw.sse_mathfun inside Rack (RP-20), against Rack's compiled code.

native/tests/simd_reference.cpp runs Rack's own vectors, operators and
functions lane by lane over edge cases and seeded random inputs; every case
replays here, bit for bit. The other tests check what the reference cannot:
Python's construction and conversion rules, views and buffers, masks, errors,
the templates and the Port methods.
"""

import array
import copy
import math
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any

import pytest

import rackpy.simd
from rackpy.raw import engine, sse_mathfun
from rackpy.raw import simd as s
from rackpy.raw.simd import float_4, int32_4


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


def signed(bits: str) -> int:
    value: int = struct.unpack("<i", struct.pack("<I", int(bits, 16)))[0]
    return value


def decode(value: Any) -> Any:
    """The C++ reference's JSON encoding of a value, as the Python value the bindings take.

    A float_4 is rebuilt from its lanes' bit patterns: inside Rack, converting a Python float to a
    C++ float flushes a subnormal to zero (Rack's FTZ mode), so float_4(1e-45) would not be the
    reference's input.
    """
    if isinstance(value, dict):
        if "f" in value:
            return f32(value["f"])
        if "f4" in value:
            return float_4.cast(int32_4(*map(signed, value["f4"])))
        if "i4" in value:
            return int32_4(*value["i4"])
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


# Rack compiles with -funsafe-math-optimizations, which implies -fno-signed-zeros, so the sign of a
# zero is not part of Rack's arithmetic semantics (ADR 009); nor is a NaN's payload. For bitwise
# operations, masks and casts the bits are the semantics, so those compare exactly.
EXACT = {
    "f4.xor",
    "f4.and",
    "f4.or",
    "f4.ixor",
    "f4.iand",
    "f4.ior",
    "f4.invert",
    "f4.eq",
    "f4.ge",
    "f4.gt",
    "f4.le",
    "f4.lt",
    "f4.ne",
    "f4.cast",
    "f4.zero",
    "f4.mask",
    "f4.load",
    "f4.lanes",
    "f4.broadcast",
    "f4.store",
    "andnot",
    "ifelse_f4",
    "movemask_inverse_f4",
    "fabs",
    "abs",
    "sse_mathfun_one_ps",
}


def same_bits(actual: int, expected: int, exact: bool) -> bool:
    """Two float lanes as 32-bit patterns: exactly, or with any NaN equal to any NaN and ±0 equal."""
    actual &= 0xFFFFFFFF
    expected &= 0xFFFFFFFF
    if exact:
        return actual == expected
    if expected & 0x7F800000 == 0x7F800000 and expected & 0x007FFFFF:
        return actual & 0x7F800000 == 0x7F800000 and bool(actual & 0x007FFFFF)
    if expected & 0x7FFFFFFF == 0:
        return actual & 0x7FFFFFFF == 0
    return actual == expected


def same_float(actual: float, expected: float, exact: bool) -> bool:
    """A scalar result. Reading a C++ float into Python flushes a subnormal to zero inside Rack."""
    if 0 < abs(expected) < 1.1754943508222875e-38:
        return actual in (0.0, expected)
    if math.isnan(expected):
        return math.isnan(actual)
    if expected == 0.0 and not exact:
        return actual == 0.0
    return actual == expected and math.copysign(1.0, actual) == math.copysign(1.0, expected)


def same(actual: Any, expected: Any, exact: bool) -> bool:
    if isinstance(expected, float):
        return isinstance(actual, float) and same_float(actual, expected, exact)
    if isinstance(expected, float_4):
        lanes = zip(int32_4.cast(actual).tolist(), int32_4.cast(expected).tolist(), strict=True)
        return isinstance(actual, float_4) and all(same_bits(a, e, exact) for a, e in lanes)
    if isinstance(expected, int32_4):
        return isinstance(actual, int32_4) and actual.tolist() == expected.tolist()
    if isinstance(expected, list):
        return (
            isinstance(actual, list | tuple)
            and len(actual) == len(expected)
            and all(same(a, e, exact) for a, e in zip(actual, expected, strict=True))
        )
    return type(actual) is type(expected) and bool(actual == expected)


def stored(v: float_4 | int32_4) -> list[Any]:
    out = array.array("f" if isinstance(v, float_4) else "i", [0] * 4)
    v.store(out)
    return out.tolist()


def in_place(op: Callable[[Any, Any], Any]) -> Callable[[Any, Any], Any]:
    def run(a: Any, b: Any) -> Any:
        before = a
        result = op(a, b)
        assert result is before, "a compound assignment changes the vector in place"
        return result

    return run


def post(method: str) -> Callable[[Any], list[Any]]:
    def run(a: Any) -> list[Any]:
        old = getattr(a, method)()
        return [old, a]

    return run


CALLS: dict[str, Callable[..., Any]] = {
    "f4.broadcast": float_4,
    "i4.broadcast": int32_4,
    "f4.lanes": float_4,
    "i4.from_f4": int32_4,
    "i4.cast": int32_4.cast,
    "f4.store": stored,
    "f4.from_i4": float_4,
    "f4.cast": float_4.cast,
    "i4.store": stored,
    "f4.zero": float_4.zero,
    "f4.mask": float_4.mask,
    "i4.zero": int32_4.zero,
    "i4.mask": int32_4.mask,
    "f4.load": lambda xs: float_4.load(array.array("f", xs)),
    "i4.load": lambda xs: int32_4.load(array.array("i", xs)),
    "f4.add": lambda a, b: a + b,
    "f4.sub": lambda a, b: a - b,
    "f4.mul": lambda a, b: a * b,
    "f4.div": lambda a, b: a / b,
    "f4.xor": lambda a, b: a ^ b,
    "f4.and": lambda a, b: a & b,
    "f4.or": lambda a, b: a | b,
    "f4.eq": lambda a, b: a == b,
    "f4.ge": lambda a, b: a >= b,
    "f4.gt": lambda a, b: a > b,
    "f4.le": lambda a, b: a <= b,
    "f4.lt": lambda a, b: a < b,
    "f4.ne": lambda a, b: a != b,
    "f4.iadd": in_place(float_4.__iadd__),
    "f4.isub": in_place(float_4.__isub__),
    "f4.imul": in_place(float_4.__imul__),
    "f4.idiv": in_place(float_4.__itruediv__),
    "f4.ixor": in_place(float_4.__ixor__),
    "f4.iand": in_place(float_4.__iand__),
    "f4.ior": in_place(float_4.__ior__),
    "f4.pos": lambda a: +a,
    "f4.neg": lambda a: -a,
    "f4.invert": lambda a: ~a,
    "f4.pre_increment": float_4.pre_increment,
    "f4.pre_decrement": float_4.pre_decrement,
    "f4.post_increment": post("post_increment"),
    "f4.post_decrement": post("post_decrement"),
    "f4.rsub": lambda a, t: t - a,
    "f4.rdiv": lambda a, t: t / a,
    "f4.mul_s": lambda a, t: a * t,
    "i4.add": lambda a, b: a + b,
    "i4.sub": lambda a, b: a - b,
    "i4.xor": lambda a, b: a ^ b,
    "i4.and": lambda a, b: a & b,
    "i4.or": lambda a, b: a | b,
    "i4.eq": lambda a, b: a == b,
    "i4.ge": lambda a, b: a >= b,
    "i4.gt": lambda a, b: a > b,
    "i4.le": lambda a, b: a <= b,
    "i4.lt": lambda a, b: a < b,
    "i4.ne": lambda a, b: a != b,
    "i4.iadd": in_place(int32_4.__iadd__),
    "i4.isub": in_place(int32_4.__isub__),
    "i4.ixor": in_place(int32_4.__ixor__),
    "i4.iand": in_place(int32_4.__iand__),
    "i4.ior": in_place(int32_4.__ior__),
    "i4.pos": lambda a: +a,
    "i4.neg": lambda a: -a,
    "i4.invert": lambda a: ~a,
    "i4.pre_increment": int32_4.pre_increment,
    "i4.pre_decrement": int32_4.pre_decrement,
    "i4.post_increment": post("post_increment"),
    "i4.post_decrement": post("post_decrement"),
    "i4.lshift": lambda a, n: a << n,
    "i4.rshift": lambda a, n: a >> n,
    "andnot": s.andnot,
    "movemask_i4": s.movemask,
    "movemask_f": s.movemask,
    "rsqrt": s.rsqrt,
    "rcp": s.rcp,
    "ifelse_b": s.ifelse,
    "ifelse_f4": s.ifelse,
    "movemask_inverse_i4": s.movemask_inverse[int32_4],
    "movemask_inverse_f4": s.movemask_inverse[float_4],
    "fmax": s.fmax,
    "fmin": s.fmin,
    "sqrt": s.sqrt,
    "log": s.log,
    "log10": s.log10,
    "log2": s.log2,
    "exp": s.exp,
    "sin": s.sin,
    "cos": s.cos,
    "tan": s.tan,
    "atan": s.atan,
    "atan2": s.atan2,
    "trunc": s.trunc,
    "floor": s.floor,
    "ceil": s.ceil,
    "round": s.round,
    "fmod": s.fmod,
    "hypot": s.hypot,
    "fabs": s.fabs,
    "abs": s.abs,
    "abs_c": lambda re, im: s.abs((re, im)),
    "arg_c": lambda re, im: s.arg((re, im)),
    "pow": s.pow,
    "pow_s": s.pow,
    "pow_i": s.pow,
    "pow_i_f": s.pow,
    "clamp": s.clamp,
    "rescale": s.rescale,
    "crossfade": s.crossfade,
    "clamp_default": s.clamp,
    "sgn": s.sgn,
    "fmax_f": s.fmax,
    "fmin_f": s.fmin,
    "sqrt_f": s.sqrt,
    "log_f": s.log,
    "log10_f": s.log10,
    "log2_f": s.log2,
    "exp_f": s.exp,
    "sin_f": s.sin,
    "cos_f": s.cos,
    "tan_f": s.tan,
    "atan_f": s.atan,
    "atan2_f": s.atan2,
    "trunc_f": s.trunc,
    "floor_f": s.floor,
    "ceil_f": s.ceil,
    "round_f": s.round,
    "fmod_f": s.fmod,
    "hypot_f": s.hypot,
    "fabs_f": s.fabs,
    "abs_f": s.abs,
    "pow_f": s.pow,
    "sgn_f": s.sgn,
    "clamp_f": s.clamp,
    "rescale_f": s.rescale,
    "crossfade_f": s.crossfade,
    "abs_cf": lambda re, im: s.abs(complex(re, im)),
    "arg_cf": lambda re, im: s.arg(complex(re, im)),
    "sse_mathfun_one_ps": sse_mathfun.sse_mathfun_one_ps,
    "sse_mathfun_log_ps": sse_mathfun.sse_mathfun_log_ps,
    "sse_mathfun_exp_ps": sse_mathfun.sse_mathfun_exp_ps,
    "sse_mathfun_sin_ps": sse_mathfun.sse_mathfun_sin_ps,
    "sse_mathfun_cos_ps": sse_mathfun.sse_mathfun_cos_ps,
    "sse_mathfun_tan_ps": sse_mathfun.sse_mathfun_tan_ps,
    "sse_mathfun_cot_ps": sse_mathfun.sse_mathfun_cot_ps,
    "sse_mathfun_atan_ps": sse_mathfun.sse_mathfun_atan_ps,
    "sse_mathfun_atan2_ps": sse_mathfun.sse_mathfun_atan2_ps,
    "sse_mathfun_sincos_ps": sse_mathfun.sse_mathfun_sincos_ps,
    "sse_mathfun_tancot_ps": sse_mathfun.sse_mathfun_tancot_ps,
}


def test_reference_is_racks_x64_implementation(reference: dict[str, Any]) -> None:
    import rackpy.raw

    assert reference["implementation"] == "sse4.2" == rackpy.simd.IMPLEMENTATION
    assert len(reference["cases"]) > 20000
    assert rackpy.raw.APP_VERSION == "2.6.6"


def subnormal_scalar(args: Any) -> bool:
    return any(isinstance(a, dict) and "f" in a and 0 < int(a["f"], 16) & 0x7FFFFFFF < 0x00800000 for a in args)


def test_every_reference_case_matches_rack(reference: dict[str, Any]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    skipped = 0
    for case in reference["cases"]:
        fn = case["fn"]
        seen[fn] += 1
        if subnormal_scalar(case["args"]):
            # A Python float cannot reach Rack as a subnormal C++ float: the conversion flushes it.
            skipped += 1
            continue
        args = decode(case["args"])
        expected = decode(case["result"])
        actual = CALLS[fn](*args)
        if not same(actual, expected, exact=fn in EXACT):
            mismatches.append(f"{fn}{tuple(args)!r}: Rack gives {expected!r}, rackpy.raw gives {actual!r}")
    total = len(reference["cases"])
    assert not mismatches, f"{len(mismatches)} of {total} cases differ:\n" + "\n".join(mismatches[:40])
    assert set(seen) == set(CALLS), "every reference function is replayed, and nothing else"
    assert skipped < total // 50, f"{skipped} scalar cases with subnormal inputs were skipped"


def test_python_floats_reach_rack_flushed_to_zero() -> None:
    # Rack's FTZ mode applies to the double -> float conversion too: build subnormal lanes from bits.
    assert float_4(1e-45).tolist() == [0.0] * 4
    tiny = float_4.cast(int32_4(1))
    assert int32_4.cast(tiny).tolist() == [1] * 4


def test_construction_lanes_and_views() -> None:
    assert float_4().tolist() == [0.0] * 4 and int32_4().tolist() == [0] * 4  # zeroed, not uninitialized
    v = float_4(1, 2.5, -3, 4)
    assert v.tolist() == [1.0, 2.5, -3.0, 4.0] and list(v) == v.tolist() and len(v) == 4
    assert float_4(2).tolist() == [2.0] * 4 and float_4(0.1)[0] == struct.unpack("<f", struct.pack("<f", 0.1))[0]
    assert repr(v) == "float_4(1.0, 2.5, -3.0, 4.0)" and repr(int32_4(1, -2, 3, 4)) == "int32_4(1, -2, 3, 4)"
    assert float_4.size == int32_4.size == 4 and float_4.type is float and int32_4.type is int
    # Lanes: indexing is checked; negative indices count from the end.
    v[1] = 7
    v[-1] = 9.5
    assert v[1] == 7.0 and v[3] == 9.5 and v[-4] == 1.0
    for bad in (4, -5, 100):
        with pytest.raises(IndexError):
            v[bad]
        with pytest.raises(IndexError):
            v[bad] = 0.0
    n = int32_4(5)
    n[0] = -(2**31)
    assert n[0] == -(2**31)
    with pytest.raises(OverflowError):
        n[1] = 2**31
    with pytest.raises(OverflowError):
        int32_4(2**31)
    with pytest.raises(TypeError):
        int32_4(2.5)  # type: ignore[call-overload]  # int overloads take Python ints only
    with pytest.raises(TypeError):
        n[2] = 1.5  # type: ignore[assignment]
    # Conversions are explicit; copies are copies.
    assert int32_4(float_4(1.9, -1.9, 2.5, float("nan"))).tolist() == [1, -1, 2, -(2**31)]
    assert float_4(int32_4(3, -4, 2**31 - 1, 0)).tolist() == [3.0, -4.0, 2147483648.0, 0.0]
    w = float_4(v)
    w[0] = 100.0
    assert v[0] == 1.0 and w is not v
    c = copy.copy(v)
    d = copy.deepcopy(v)
    c[0] = d[0] = -1.0
    assert v[0] == 1.0
    # s is a live view of the lanes; v is the register, read as a copy and assigned whole.
    lanes = v.s
    assert lanes.format == "f" and lanes.tolist() == v.tolist() and not lanes.readonly
    lanes[2] = 42.0  # type: ignore[call-overload]  # typeshed types memoryview items as int
    assert v[2] == 42.0
    del v
    assert lanes[2] == 42.0  # the view keeps the vector alive
    x = float_4(1, 2, 3, 4)
    register = x.v
    register[0] = 0.0
    assert x[0] == 1.0
    x.v = float_4(5)
    assert x.tolist() == [5.0] * 4
    with pytest.raises(TypeError):
        x.v = 1.0  # type: ignore[assignment]  # a register is a vector; Rack has no float to __m128
    i = int32_4(1, 2, 3, 4)
    assert i.s.format == "i" and i.s.itemsize == 4 and i.v.tolist() == [1, 2, 3, 4]
    # Values: mutable, so unhashable; a vector has no truth value.
    with pytest.raises(TypeError):
        hash(x)
    with pytest.raises(TypeError):
        bool(x)
    with pytest.raises(TypeError):
        bool(int32_4(1))


def test_buffers_load_and_store() -> None:
    data = array.array("f", [1.0, 2.0, 3.0, 4.0, 5.0])
    assert float_4.load(data).tolist() == [1.0, 2.0, 3.0, 4.0]  # x[0] in lane 0
    assert float_4.load(memoryview(data)[1:]).tolist() == [2.0, 3.0, 4.0, 5.0]
    # Any alignment: Rack uses unaligned loads and stores.
    raw = bytearray(1) + bytearray(struct.pack("<4f", 1.5, 2.5, 3.5, 4.5))
    misaligned = memoryview(raw)[1:].cast("f")
    assert float_4.load(misaligned).tolist() == [1.5, 2.5, 3.5, 4.5]
    float_4(-1.0).store(misaligned)
    assert struct.unpack("<4f", bytes(raw[1:])) == (-1.0,) * 4
    out = array.array("f", [0.0] * 6)
    float_4(1, 2, 3, 4).store(memoryview(out)[2:])
    assert out.tolist() == [0.0, 0.0, 1.0, 2.0, 3.0, 4.0]
    ints = array.array("i", [7, 8, 9, 10])
    assert int32_4.load(ints).tolist() == [7, 8, 9, 10]
    int32_4(-1).store(ints)
    assert ints.tolist() == [-1] * 4
    # The buffer protocol on the vectors themselves.
    v = float_4(1, 2, 3, 4)
    assert float_4.load(v).tolist() == v.tolist() and bytes(memoryview(v)) == struct.pack("<4f", 1, 2, 3, 4)
    # Unsafe buffers fail before Rack reads or writes past them.
    with pytest.raises(ValueError, match="at least 4"):
        float_4.load(array.array("f", [1.0, 2.0, 3.0]))
    with pytest.raises(ValueError, match="at least 4"):
        float_4(1.0).store(memoryview(out)[3:])
    with pytest.raises(TypeError, match="float32"):
        float_4.load(array.array("d", [1.0] * 4))
    with pytest.raises(TypeError, match="int32"):
        int32_4.load(array.array("f", [1.0] * 4))
    with pytest.raises(TypeError):
        float_4.load(bytes(16))  # bytes are 'B' items
    with pytest.raises(ValueError, match="contiguous"):
        float_4.load(memoryview(array.array("f", [0.0] * 8))[::2])
    with pytest.raises(BufferError):
        float_4(1.0).store(memoryview(array.array("f", [0.0] * 4)).toreadonly())
    with pytest.raises(TypeError):
        float_4.load([1.0, 2.0, 3.0, 4.0])  # type: ignore[arg-type]  # a list is not a buffer


def test_operators_and_broadcasting() -> None:
    a = float_4(1, 2, 3, 4)
    # A scalar broadcasts on either side, as C++'s Vector(float) does.
    assert (a + 1).tolist() == [2.0, 3.0, 4.0, 5.0] and (1 + a).tolist() == [2.0, 3.0, 4.0, 5.0]
    assert (2.0 * a).tolist() == [2.0, 4.0, 6.0, 8.0] and (10 - a).tolist() == [9.0, 8.0, 7.0, 6.0]
    assert (12 / a).tolist() == [12.0, 6.0, 4.0, 3.0] and (a / 2).tolist() == [0.5, 1.0, 1.5, 2.0]
    i = int32_4(1, 2, 3, 4)
    assert (i + 1).tolist() == [2, 3, 4, 5] and (10 - i).tolist() == [9, 8, 7, 6]
    assert (i << 2).tolist() == [4, 8, 12, 16] and (int32_4(-8) >> 1).tolist() == [0x7FFFFFFC] * 4
    assert (i << 32).tolist() == [0] * 4 and (i << -1).tolist() == [0] * 4  # out-of-range counts give 0
    # Comparisons give masks, not bools.
    mask = a > 2
    assert isinstance(mask, float_4) and s.movemask(mask) == 0b1100
    assert int32_4.cast(mask).tolist() == [0, 0, -1, -1]
    assert (i == int32_4(1, 0, 3, 0)).tolist() == [-1, 0, -1, 0]
    assert s.movemask(3 < a) == 0b1000  # noqa: SIM300  # reflected: Python asks a.__gt__(3)
    equal: object = a == "x"
    assert equal is False  # not a vector: Python's identity fallback
    # Rack's own idiom: subtract 1 where x >= 1.
    x = float_4(0.5, 1.0, 1.5, 3.0)
    x -= (x >= 1.0) & 1.0
    assert x.tolist() == [0.5, 0.0, 0.5, 2.0]
    # float_4 and int32_4 never mix implicitly (C++ rejects it as ambiguous), and int32_4 has no * or /.
    mixed = (lambda: a + i, lambda: i + a, lambda: i * i, lambda: i / i, lambda: i + 0.5, lambda: a << 1)  # type: ignore[operator]
    for bad in mixed:
        with pytest.raises(TypeError):
            bad()
    # In place: the same object changes, as C++'s operators return a reference.
    before = a
    a += float_4(1)
    a *= 2
    assert a is before and a.tolist() == [4.0, 6.0, 8.0, 10.0]
    j = i
    j ^= int32_4(1)
    assert j is i and i.tolist() == [0, 3, 2, 5]
    assert (~int32_4(0)).tolist() == [-1] * 4 and (-i).tolist() == [0, -3, -2, -5] and (+i).tolist() == i.tolist()
    assert (int32_4(2**31 - 1) + 1).tolist() == [-(2**31)] * 4  # wraps, as SSE adds


def test_increment_and_decrement() -> None:
    a = float_4(1, 2, 3, 4)
    assert a.pre_increment() is a and a.tolist() == [2.0, 3.0, 4.0, 5.0]
    old = a.post_decrement()
    assert old.tolist() == [2.0, 3.0, 4.0, 5.0] and a.tolist() == [1.0, 2.0, 3.0, 4.0] and old is not a
    i = int32_4(2**31 - 1)
    i.pre_increment()
    assert i.tolist() == [-(2**31)] * 4
    assert i.post_increment().tolist() == [-(2**31)] * 4 and i[0] == -(2**31) + 1
    assert i.pre_decrement().tolist() == [-(2**31)] * 4


def test_vector_template() -> None:
    assert s.Vector[float, 4] is float_4 and s.Vector[int, 4] is int32_4
    assert set(s.Vector.specializations.values()) == {float_4, int32_4}
    for bad in ((float, 8), (int, 2), (complex, 4), float):
        with pytest.raises(TypeError, match="not defined in Rack"):
            s.Vector[bad]
    with pytest.raises(TypeError, match="template"):
        s.Vector(1.0)
    assert rackpy.simd.float_4 is float_4 and rackpy.simd.Vector is s.Vector


def test_functions_dispatch_scalars_and_vectors() -> None:
    # A Python float picks the std:: function (computed in C++ float), a vector Rack's.
    assert s.sin(0.5) == struct.unpack("<f", struct.pack("<f", math.sin(0.5)))[0]
    assert isinstance(s.sin(0.5), float) and isinstance(s.sin(float_4(0.5)), float_4)
    assert s.fmax(1.0, 2.0) == 2.0 and s.fmax(float_4(1.0), 3).tolist() == [3.0] * 4  # mixed: broadcast

    # Generic code works for both, as Rack intends.
    def sin_plus_cos(x: Any) -> Any:
        return s.sin(x) + s.cos(x)

    assert isinstance(sin_plus_cos(1.0), float) and isinstance(sin_plus_cos(float_4(1.0)), float_4)
    # Rack's documented differences from std::: round ties to even, fmod is floored, sin(inf) is -inf.
    assert s.round(float_4(0.5, 1.5, 2.5, -0.5)).tolist() == [0.0, 2.0, 2.0, -0.0]
    assert s.round(2.5) == 3.0  # std::round, for a scalar, rounds ties away from zero
    assert s.fmod(float_4(-1.0), 3.0).tolist() == [2.0] * 4 and s.fmod(-1.0, 3.0) == -1.0
    assert s.sin(float_4(math.inf, -math.inf, 0.0, 0.0)).tolist()[:2] == [-math.inf, math.inf]
    # sse_mathfun's edge cases: log(0) is NaN, exp saturates (even for inf and NaN), so pow(a <= 0, b) is huge.
    assert all(map(math.isnan, s.log(float_4(0.0, -0.0, -1.0, -math.inf))))
    ceiling = s.exp(float_4(math.inf))[0]
    assert 2.40e38 < ceiling < 2.41e38 and s.exp(float_4(math.nan)).tolist() == [ceiling] * 4
    assert list(s.pow(float_4(0.0, -2.0, 4.0, 1.0), float_4(1 / 3)))[:2] == [ceiling, ceiling]
    # fmax/fmin give the second argument's lane where either is NaN (_mm_max_ps, _mm_min_ps).
    assert s.fmax(float_4(math.nan), 1.0).tolist() == [1.0] * 4 and math.isnan(s.fmax(float_4(1.0), math.nan)[0])
    assert s.fmin(float_4(math.nan), -1.0).tolist() == [-1.0] * 4 and math.isnan(s.fmin(float_4(1.0), math.nan)[0])
    # pow: int exponents use Rack's squaring template, with its limits.
    assert s.pow(float_4(2.0), 10).tolist() == [1024.0] * 4 and s.pow(2.0, 10) == 1024.0
    assert s.pow(float_4(2.0), -1).tolist() == [1.0] * 4 and s.pow(3.0, 0) == 1.0  # b <= 0 gives 1, as in Rack
    with pytest.raises(ValueError, match="2\\*\\*30"):
        s.pow(2.0, 2**30)
    with pytest.raises(OverflowError):
        s.pow(float_4(2.0), 2**40)
    assert s.pow(2.0, float_4(3.0))[0] == pytest.approx(8.0, rel=1e-6)
    assert s.pow(float_4(2.0), 0.5)[0] == pytest.approx(math.sqrt(2), rel=1e-6)
    # Masks and selection.
    assert s.movemask(float_4.mask()) == 0xF == s.movemask(int32_4.mask()) and s.movemask(float_4(1.0)) == 0
    assert s.movemask(float_4(-1.0, 1.0, -0.0, 0.0)) == 0b0101  # the sign bit, not the value
    m = s.movemask_inverse[float_4](0b0110)
    assert s.movemask(m) == 0b0110 and s.ifelse(m, 1.0, 2.0).tolist() == [2.0, 1.0, 1.0, 2.0]
    assert s.movemask_inverse[int32_4](0b1001).tolist() == [-1, 0, 0, -1]
    assert s.ifelse(True, 1.0, 2.0) == 1.0 and s.ifelse(False, 1.0, 2.0) == 2.0
    with pytest.raises(TypeError, match="not defined in Rack"):
        s.movemask_inverse[float]
    with pytest.raises(TypeError, match="template"):
        s.movemask_inverse(3)
    # std::complex<float_4> is a (real, imag) pair; Python complex is std::complex<float>.
    re, im = float_4(3.0), float_4(4.0)
    assert s.abs((re, im)).tolist() == [5.0] * 4 and s.abs(complex(3, 4)) == 5.0
    assert s.arg((float_4(0.0), float_4(1.0)))[0] == pytest.approx(math.pi / 2, rel=1e-6)
    with pytest.raises(TypeError):
        s.abs((re,))  # type: ignore[arg-type]
    # The math:: imports and their defaults.
    assert s.clamp(float_4(-1, 0.5, 2, 1)).tolist() == [0.0, 0.5, 1.0, 1.0] and s.clamp(5.0, 0.0, 3.0) == 3.0
    assert s.sgn(float_4(-2.0, 0.0, 3.0, -0.0)).tolist() == [-1.0, 0.0, 1.0, 0.0] and s.sgn(-3.0) == -1.0
    assert s.crossfade(float_4(0.0), float_4(10.0), 0.25).tolist() == [2.5] * 4
    assert s.rescale(float_4(5.0), 0, 10, -1, 1).tolist() == [0.0] * 4
    assert s.atan2(1.0, 0.0) == pytest.approx(math.pi / 2)
    assert s.atan2(float_4(1.0), 0.0)[0] == pytest.approx(math.pi / 2)


def test_sse_mathfun() -> None:
    one = sse_mathfun.sse_mathfun_one_ps()
    assert one.tolist() == [1.0] * 4
    sin, cos = sse_mathfun.sse_mathfun_sincos_ps(float_4(0.0, 1.0, 2.0, 3.0))
    assert sin.tolist() == sse_mathfun.sse_mathfun_sin_ps(float_4(0.0, 1.0, 2.0, 3.0)).tolist()
    assert cos[0] == 1.0 and abs(sin[1] - math.sin(1.0)) < 1e-6
    assert sse_mathfun.sse_mathfun_tancot_ps(float_4(1.0), 1).tolist() == sse_mathfun.sse_mathfun_cot_ps(1.0).tolist()
    # Rack's simd.atan2(x, y) passes its arguments to sse_mathfun_atan2_ps(y, x) in order.
    assert sse_mathfun.sse_mathfun_atan2_ps(1.0, 0.0).tolist() == s.atan2(float_4(1.0), 0.0).tolist()
    assert s.log(float_4(2.0)).tolist() == sse_mathfun.sse_mathfun_log_ps(2.0).tolist()
    with pytest.raises(OverflowError):
        sse_mathfun.sse_mathfun_tancot_ps(1.0, 2**40)


def test_port_simd_methods() -> None:
    p = engine.Port()
    for c in range(16):
        p.set_voltage(float(c), c)
    assert p.get_voltage_simd(0).tolist() == [0.0, 1.0, 2.0, 3.0]
    assert p.get_voltage_simd(12).tolist() == [12.0, 13.0, 14.0, 15.0]
    # A port Python created starts disconnected (0 channels): normalling gives the normal voltage.
    assert not p.is_connected()
    assert p.get_normal_voltage_simd(float_4(-5.0), 4).tolist() == [-5.0] * 4
    assert p.get_normal_poly_voltage_simd(9.0, 4).tolist() == [9.0] * 4  # a scalar broadcasts
    with pytest.warns(DeprecationWarning):
        p.channels = 1
    # Monophonic: the poly read broadcasts channel 0.
    assert p.get_poly_voltage_simd(8).tolist() == [0.0] * 4
    assert p.get_normal_voltage_simd(float_4(-5.0), 4).tolist() == [4.0, 5.0, 6.0, 7.0]
    with pytest.warns(DeprecationWarning):
        p.channels = 16
    assert p.get_poly_voltage_simd(8).tolist() == [8.0, 9.0, 10.0, 11.0]
    assert p.get_normal_poly_voltage_simd(-1.0, 8).tolist() == [8.0, 9.0, 10.0, 11.0]
    p.set_voltage_simd(float_4(-1, -2, -3, -4), 2)
    assert [p.get_voltage(c) for c in range(1, 7)] == [1.0, -1.0, -2.0, -3.0, -4.0, 6.0]
    assert p.get_channels() == 16  # setVoltageSimd leaves the channel count alone
    out = engine.Output()
    out.set_voltage_simd(3.0, 0)
    assert out.get_voltage_simd(0).tolist() == [3.0] * 4


def test_port_simd_channels_are_checked() -> None:
    p = engine.Port()
    for bad in (13, 16, -1, 255):
        with pytest.raises(IndexError, match="0 to 12"):
            p.get_voltage_simd(bad)
        with pytest.raises(IndexError, match="0 to 12"):
            p.set_voltage_simd(float_4(1.0), bad)
        with pytest.raises(IndexError, match="0 to 12"):
            p.get_normal_poly_voltage_simd(float_4(1.0), bad)
    assert [p.get_voltage(c) for c in range(16)] == [0.0] * 16
    with pytest.raises(TypeError):
        p.set_voltage_simd(int32_4(1), 0)  # type: ignore[arg-type]
