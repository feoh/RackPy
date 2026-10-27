# SPDX-License-Identifier: GPL-3.0-or-later
"""Every case from the compiled C++ reference, replayed through rackpy.raw inside Rack, bit for bit.

native/tests/values_reference.cpp runs Rack's own code over edge-case inputs
(signed zeros, denormals, infinities, NaN, integer extremes); the bindings
must give the same results, which shows they call Rack rather than a Python
re-implementation that could drift.
"""

import math
import struct
from collections import Counter
from collections.abc import Callable
from typing import Any

import rackpy.raw.math as m
from rackpy.raw import color
from rackpy.raw import random as rr
from rackpy.raw.math import Rect, Vec
from rackpy.raw.nanovg import NVGcolor
from rackpy.types import ValueQuantity


def f32(bits: str) -> float:
    value: float = struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0]
    return value


class F64(float):
    """A C++ double from the reference, compared as 64 bits rather than as a float's 32."""


def f64(bits: str) -> F64:
    value: float = struct.unpack("<d", struct.pack("<Q", int(bits, 16)))[0]
    return F64(value)


def decode(value: Any) -> Any:
    """The C++ reference's JSON encoding of a value, as the Python value the bindings take."""
    if isinstance(value, dict):
        if "f" in value:
            return f32(value["f"])
        if "d" in value:
            return f64(value["d"])
        if "vec" in value:
            return Vec(*map(decode, value["vec"]))
        if "rect" in value:
            return Rect(*map(decode, value["rect"]))
        if "color" in value:
            return NVGcolor(*map(decode, value["color"]))
    if isinstance(value, list):
        return [decode(v) for v in value]
    return value


# Rack compiles with -funsafe-math-optimizations, which implies -fno-signed-zeros: the sign of a
# zero result is not part of Rack's semantics, and two builds of the same function (GCC 13 against
# GCC 16, or two call sites) may differ in it. Only normalize_zero, which Rack compiles with
# optimize("signed-zeros"), promises a sign, so only its zeros are compared with their sign.
SIGNED_ZEROS = {"normalize_zero"}


def same_float(actual: float, expected: float, signed_zeros: bool) -> bool:
    """Equal bit patterns; any NaN equals any NaN (payloads are not API); ±0 equal unless signed_zeros."""
    if math.isnan(expected):
        return math.isnan(actual)
    if expected == 0.0 and not signed_zeros:
        return actual == 0.0
    width = "<d" if isinstance(expected, F64) else "<f"
    return struct.pack(width, actual) == struct.pack(width, expected)


def same(actual: Any, expected: Any, signed_zeros: bool = False) -> bool:
    if isinstance(expected, float):
        return isinstance(actual, float) and same_float(actual, expected, signed_zeros)
    if isinstance(expected, Vec):
        return isinstance(actual, Vec) and same(actual.x, expected.x) and same(actual.y, expected.y)
    if isinstance(expected, Rect):
        return isinstance(actual, Rect) and same(actual.pos, expected.pos) and same(actual.size, expected.size)
    if isinstance(expected, NVGcolor):
        return isinstance(actual, NVGcolor) and all(map(same, actual.rgba, expected.rgba, strict=True))
    if isinstance(expected, list):
        return (
            isinstance(actual, list | tuple)
            and len(actual) == len(expected)
            and all(map(same, actual, expected, strict=True))
        )
    return type(actual) is type(expected) and bool(actual == expected)


def quantity(lo: float, hi: float, default: float, label: str, unit: str, value: float | None = None) -> ValueQuantity:
    q = ValueQuantity(lo, hi, default, label, unit)
    if value is not None:
        q.set_value(value)
    return q


def q_state(lo: float, hi: float, d: float, label: str, unit: str, x: float) -> list[Any]:
    q = quantity(lo, hi, d, label, unit, x)
    return [
        q.get_value(),
        q.get_display_value_string(),
        q.get_string(),
        q.is_min(),
        q.is_max(),
        q.get_scaled_value(),
        q.is_bounded(),
        q.get_range(),
    ]


def q_after(action: Callable[[ValueQuantity], object]) -> Callable[..., float]:
    def run(lo: float, hi: float, d: float, label: str, unit: str, x: float) -> float:
        q = quantity(lo, hi, d, label, unit, x)
        action(q)
        return q.get_value()

    return run


def q_set_display(lo: float, hi: float, d: float, label: str, unit: str, text: str) -> float:
    q = quantity(lo, hi, d, label, unit)
    q.set_display_value_string(text)
    return q.get_value()


def q_from_max(action: Callable[[ValueQuantity], object]) -> Callable[..., float]:
    """The reference's reset, set_min and set_max cases start from the maximum value."""

    def run(lo: float, hi: float, d: float, label: str, unit: str) -> float:
        q = quantity(lo, hi, d, label, unit, hi)
        action(q)
        return q.get_value()

    return run


def random_get(s0: int, s1: int, type: str) -> list[Any]:
    """Eight draws of get(type) from Rack's own generator, seeded as the reference seeds it; its state is restored."""
    generator = rr.local()
    saved = generator.state
    try:
        generator.seed(s0, s1)
        return [rr.get(type) for _ in range(8)]
    finally:
        generator.state = saved


CALLS: dict[str, Callable[..., Any]] = {
    "clamp_f": m.clamp,
    "clamp_safe_f": m.clamp_safe,
    "clamp_f_default": m.clamp,
    "normalize_zero": m.normalize_zero,
    "sgn_f": m.sgn,
    "chop": m.chop,
    "euc_mod_f": m.euc_mod,
    "is_near": m.is_near,
    "crossfade_f": m.crossfade,
    "complex_mult": m.complex_mult,
    "rescale": m.rescale,
    "interpolate_linear": m.interpolate_linear,
    "clamp_i": m.clamp,
    "clamp_safe_i": m.clamp_safe,
    "euc_mod_i": m.euc_mod,
    "euc_div": m.euc_div,
    "euc_div_mod": m.euc_div_mod,
    "log2": m.log2,
    "is_even": m.is_even,
    "is_odd": m.is_odd,
    "is_pow2": m.is_pow2,
    "sgn_i": m.sgn,
    "sgn_d": lambda x: m.sgn(x, type="double"),
    "sgn_f_of_d": m.sgn,
    "random.get": random_get,
    "vec.neg": Vec.neg,
    "vec.arg": Vec.arg,
    "vec.norm": Vec.norm,
    "vec.normalize": Vec.normalize,
    "vec.square": Vec.square,
    "vec.area": Vec.area,
    "vec.flip": Vec.flip,
    "vec.abs": Vec.abs,
    "vec.round": Vec.round,
    "vec.floor": Vec.floor,
    "vec.ceil": Vec.ceil,
    "vec.is_zero": Vec.is_zero,
    "vec.is_finite": Vec.is_finite,
    "vec.rotate": Vec.rotate,
    "vec.mult_s": lambda a, s: a.mult(s),
    "vec.div_s": lambda a, s: a.div(s),
    "vec.rmul": lambda s, a: s * a,
    "vec.clamp": Vec.clamp,
    "vec.clamp_safe": Vec.clamp_safe,
    "vec.plus": lambda a, b: a + b,
    "vec.minus": lambda a, b: a - b,
    "vec.mult_v": lambda a, b: a * b,
    "vec.div_v": lambda a, b: a / b,
    "vec.dot": Vec.dot,
    "vec.min": Vec.min,
    "vec.max": Vec.max,
    "vec.equals": Vec.equals,
    "vec.crossfade": Vec.crossfade,
    "rect.left": Rect.get_left,
    "rect.right": Rect.get_right,
    "rect.top": Rect.get_top,
    "rect.bottom": Rect.get_bottom,
    "rect.width": Rect.get_width,
    "rect.height": Rect.get_height,
    "rect.center": Rect.get_center,
    "rect.top_left": Rect.get_top_left,
    "rect.top_right": Rect.get_top_right,
    "rect.bottom_left": Rect.get_bottom_left,
    "rect.bottom_right": Rect.get_bottom_right,
    "rect.zero_pos": Rect.zero_pos,
    "rect.grow": Rect.grow,
    "rect.shrink": Rect.shrink,
    "rect.interpolate": Rect.interpolate,
    "rect.contains_v": Rect.contains,
    "rect.contains_r": Rect.contains,
    "rect.intersects": Rect.intersects,
    "rect.equals": Rect.equals,
    "rect.clamp": Rect.clamp,
    "rect.nudge": Rect.nudge,
    "rect.expand": Rect.expand,
    "rect.intersect": Rect.intersect,
    "rect.from_min_max": Rect.from_min_max,
    "rect.from_corners": Rect.from_corners,
    "rect.inf": Rect.inf,
    "color.clamp": color.clamp,
    "color.to_hex_string": color.to_hex_string,
    "color.mult_s": color.mult,
    "color.alpha": color.alpha,
    "color.is_equal": color.is_equal,
    "color.minus": color.minus,
    "color.plus": color.plus,
    "color.mult_c": color.mult,
    "color.lerp": color.lerp,
    "color.screen": color.screen,
    "color.from_hex_string": color.from_hex_string,
    "color.constant": lambda name: getattr(color, name),
    "quantity.set_value": q_state,
    "quantity.set_scaled_value": q_after(lambda q: q.set_scaled_value(0.75)),
    "quantity.move_scaled_value": q_after(lambda q: q.move_scaled_value(0.1)),
    "quantity.move_value": q_after(lambda q: q.move_value(1.5)),
    "quantity.toggle": q_after(lambda q: q.toggle()),
    "quantity.to_scaled": lambda lo, hi, d, label, unit, x: quantity(lo, hi, d, label, unit).to_scaled(x),
    "quantity.from_scaled": lambda lo, hi, d, label, unit, x: quantity(lo, hi, d, label, unit).from_scaled(x),
    "quantity.set_display_value_string": q_set_display,
    "quantity.reset": q_from_max(lambda q: q.reset()),
    "quantity.set_min": q_from_max(lambda q: q.set_min()),
    "quantity.set_max": q_from_max(lambda q: q.set_max()),
}


def test_reference_is_the_running_rack_version(reference: list[dict[str, Any]]) -> None:
    import rackpy.raw

    assert len(reference) > 7000
    assert rackpy.raw.APP_VERSION == "2.6.6"


def test_normalize_zero_keeps_its_promise_about_signs() -> None:
    assert math.copysign(1.0, m.normalize_zero(-0.0)) == 1.0
    assert math.copysign(1.0, m.normalize_zero(0.0)) == 1.0


def test_every_reference_case_matches_rack(reference: list[dict[str, Any]]) -> None:
    mismatches: list[str] = []
    seen: Counter[str] = Counter()
    for case in reference:
        fn = case["fn"]
        seen[fn] += 1
        args = decode(case["args"])
        expected = decode(case["result"])
        actual = CALLS[fn](*args)
        if not same(actual, expected, signed_zeros=fn in SIGNED_ZEROS):
            mismatches.append(f"{fn}{tuple(args)!r}: Rack gives {expected!r}, rackpy.raw gives {actual!r}")
    assert not mismatches, f"{len(mismatches)} of {len(reference)} cases differ:\n" + "\n".join(mismatches[:40])
    assert set(seen) == set(CALLS), "every reference function is replayed, and nothing else"
