# SPDX-License-Identifier: GPL-3.0-or-later
"""rackpy.raw.math inside Rack: overload dispatch, checked inputs, views and Python conventions.

Numeric results are compared with Rack's own in test_reference_replay.py; this
file covers what the reference cannot: which overload a Python call reaches,
what happens to bad input, and the copy-versus-view rules.
"""

import copy
import math

import pytest

import rackpy.math as facade
import rackpy.raw.math as m
from rackpy.raw.math import Rect, Vec


def test_int_and_float_overloads_are_reached_by_argument_type() -> None:
    assert m.clamp(15, 0, 10) == 10 and isinstance(m.clamp(15, 0, 10), int)
    assert m.clamp(1.5) == 1.0 and isinstance(m.clamp(1.5), float)
    assert m.clamp(1, 0.5, 2) == 1.0 and isinstance(m.clamp(1, 0.5, 2), float)  # no int overload fits
    assert m.clamp_safe(5, 10, 0) == 5 and m.clamp_safe(-3.0, 1.0, -1.0) == -1.0
    assert m.euc_mod(-7, 3) == 2 and isinstance(m.euc_mod(-7, 3), int)
    assert m.euc_mod(-7.0, 3.0) == 2.0
    assert m.sgn(-5) == -1 and isinstance(m.sgn(-5), int)
    assert m.sgn(-5.0) == -1.0 and isinstance(m.sgn(-5.0), float)


def test_int_overloads_refuse_values_outside_the_cpp_type() -> None:
    with pytest.raises(OverflowError, match="does not fit the C\\+\\+ type int"):
        m.clamp(2**40, 0, 10)
    with pytest.raises(OverflowError, match="int64_t"):
        m.is_even(2**70)
    with pytest.raises(TypeError):
        m.is_even(2.0)  # type: ignore[arg-type]
    with pytest.raises(TypeError):
        m.log2("8")  # type: ignore[arg-type]


def test_preconditions_rack_only_documents_are_checked() -> None:
    for fn in (m.euc_div, m.euc_div_mod):
        with pytest.raises(ValueError, match="b must be positive"):
            fn(7, 0)
    with pytest.raises(ValueError, match="b must be positive"):
        m.euc_mod(7, -2)
    # Rack's log2 loops forever on a negative n.
    with pytest.raises(ValueError, match="must not be negative"):
        m.log2(-1)
    assert m.log2(0) == 0 and m.log2(1) == 0 and m.log2(1024) == 10
    with pytest.raises(IndexError):
        m.interpolate_linear([1.0, 2.0], 1.0)  # needs floor(x) + 2 values
    with pytest.raises(IndexError):
        m.interpolate_linear([1.0, 2.0], -0.5)
    with pytest.raises(IndexError):
        m.interpolate_linear([1.0, 2.0], math.nan)
    for huge in (3e9, math.inf, 1e38):  # converting these to int would be undefined behaviour in C++
        with pytest.raises(IndexError):
            m.interpolate_linear([1.0, 2.0, 3.0], huge)
    with pytest.raises(IndexError):
        m.interpolate_linear([], 0.0)
    assert m.interpolate_linear([1.0, 3.0, 5.0], 1.5) == 4.0


def test_floats_are_cpp_floats() -> None:
    # Arguments round to float32; magnitudes beyond its range become infinite, as a C++ float cast does.
    assert m.crossfade(0.1, 0.1, 0.0) == 0.10000000149011612
    assert m.clamp(1e300, 0.0, math.inf) == math.inf
    assert math.isnan(m.crossfade(1e300, 0.0, 0.0))  # inf + (-inf * 0)
    # Rack divides by an empty range: -1/0 is -inf, and 0/0 is NaN.
    assert m.rescale(1.0, 2.0, 2.0, 0.0, 1.0) == -math.inf
    assert math.isnan(m.rescale(2.0, 2.0, 2.0, 0.0, 1.0))
    assert m.normalize_zero(-0.0) == 0.0 and math.copysign(1.0, m.normalize_zero(-0.0)) == 1.0


def test_out_parameters_become_return_values() -> None:
    assert m.euc_div_mod(-7, 3) == (-3, 2)
    assert m.complex_mult(1.0, 2.0, 3.0, 4.0) == (-5.0, 10.0)


def test_vec_construction_fields_and_indexing() -> None:
    assert (Vec().x, Vec().y) == (0.0, 0.0)
    assert (Vec(2.5).x, Vec(2.5).y) == (2.5, 2.5)
    v = Vec(1, 2)
    v.x = 4
    v[1] = 5
    assert (v[0], v[1]) == (4.0, 5.0) and tuple(v) == (4.0, 5.0) and len(v) == 2
    with pytest.raises(IndexError):
        v[2]
    with pytest.raises(IndexError):
        v[-1] = 0.0
    assert repr(Vec(0.1, -2)) == "Vec(0.10000000149011612, -2.0)"


def test_vec_operators_are_racks() -> None:
    a, b = Vec(1, 2), Vec(3, 5)
    assert +a == a and -a == Vec(-1, -2)
    assert a + b == Vec(4, 7) and b - a == Vec(2, 3)
    assert a * b == Vec(3, 10) and a * 2 == Vec(2, 4) and 2 * a == Vec(2, 4)
    assert b / a == Vec(3, 2.5) and b / 2 == Vec(1.5, 2.5)
    assert a != b and (a == b) is False
    assert a.is_equal(Vec(1, 2)) and a.equals(Vec(1, 2))
    # The methods with a Vec are separate overloads from the operators and from the float methods.
    assert a.plus(b) == Vec(4, 7) and b.minus(a) == Vec(2, 3)
    assert a.mult(b) == Vec(3, 10) and a.mult(2) == Vec(2, 4)
    assert b.div(a) == Vec(3, 2.5) and b.div(2) == Vec(1.5, 2.5)


def test_vec_in_place_operators_change_the_value_itself() -> None:
    v = Vec(1, 1)
    alias = v
    v += Vec(1, 2)
    v -= Vec(0, 1)
    v *= 3
    v *= Vec(1, 2)
    v /= 2
    v /= Vec(1, 3)
    assert alias is v and v == Vec(3, 2)


def test_vec_is_a_mutable_value() -> None:
    a = Vec(1, 2)
    b = copy.copy(a)
    b.x = 9
    assert a.x == 1.0 and copy.deepcopy(a) == a and copy.deepcopy(a) is not a
    with pytest.raises(TypeError):
        hash(a)
    # Methods return new values; rotate and crossfade are non-const in C++ but do not modify either.
    a.rotate(1.0)
    a.crossfade(Vec(5, 5), 0.5)
    assert a == Vec(1, 2)


def test_rect_fields_are_live_views() -> None:
    r = Rect(1, 2, 3, 4)
    r.pos.x = 10  # a view: writes through to r, as in C++
    size = r.size
    size.y = 40
    assert (r == Rect(10, 2, 3, 40)) is True and (r != Rect(10, 2, 3, 40)) is False
    assert (r != Rect(10, 2, 3, 41)) is True and (r == Rect(10, 2, 3, 41)) is False
    del r
    assert size.y == 40  # the view keeps its rectangle alive
    s = Rect(Vec(1, 2), Vec(3, 4))
    s.pos = Vec(7, 8)  # assignment copies the value in
    assert s.pos == Vec(7, 8) and Rect() == Rect(0, 0, 0, 0)


def test_rect_overloads_and_infinite_rects() -> None:
    r = Rect(0, 0, 10, 10)
    assert r.contains(Vec(0, 0)) and not r.contains(Vec(10, 5))  # left/top inclusive, right/bottom exclusive
    assert r.contains(Rect(1, 1, 2, 2)) and not r.contains(Rect(5, 5, 10, 10))
    assert r.is_containing(Vec(5, 5)) and r.is_intersecting(Rect(9, 9, 5, 5)) and r.is_equal(Rect(0, 0, 10, 10))
    inf = Rect.inf()
    assert inf.contains(Vec(1e30, -1e30)) and inf.get_right() == math.inf
    assert math.isnan(inf.get_center().x)  # documented by Rack


def test_rect_facade_properties_match_the_getters() -> None:
    r = facade.Rect(1, 2, 3, 4)
    assert (r.left, r.right, r.top, r.bottom) == (r.get_left(), r.get_right(), r.get_top(), r.get_bottom())
    assert (r.width, r.height, r.center) == (3.0, 4.0, r.get_center())
    assert repr(r) == "Rect(1.0, 2.0, 3.0, 4.0)"
    with pytest.raises(TypeError):
        hash(r)
    assert facade.Vec is Vec and facade.clamp is m.clamp


def test_rack_flushes_denormals_for_all_float_arithmetic() -> None:
    # Rack's main() turns on flush-to-zero and denormals-are-zero; Python floats in Rack see it too.
    assert math.ldexp(1.0, -1070) == 0.0
    assert m.sgn(1e-45) == 0.0  # a denormal float32 argument reads as zero
    assert math.ldexp(1.0, -1000) > 0.0  # normal numbers are unaffected
