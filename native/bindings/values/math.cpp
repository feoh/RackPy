// SPDX-License-Identifier: GPL-3.0-or-later
// rack::math (math.hpp) -> rackpy.raw.math. Every function calls Rack's own
// inline implementation, compiled with the SDK's flags, so results are Rack's
// bit for bit (tests/values compares them with a compiled C++ reference).
//
// Overloads stay reachable by argument type: the int overloads take Python
// ints only (and raise OverflowError outside the C++ type's range), the float
// overloads take floats, and ints where no int overload applies.
#include <pybind11/operators.h>
#include <pybind11/stl.h>

#include <cmath>
#include <vector>

#include "values.hpp"

namespace rackpy {
namespace values {

using rack::math::Rect;
using rack::math::Vec;

void raiseOverflow(const char* what, const char* type) {
	PyErr_Format(PyExc_OverflowError, "%s: value does not fit the C++ type %s", what, type);
	throw py::error_already_set();
}

std::string floatRepr(float value) {
	return py::repr(py::float_((double) value)).cast<std::string>();
}

namespace {

int cint(const py::int_& v, const char* what) {
	return toInt<int>(v, what, "int");
}

int64_t i64(const py::int_& v, const char* what) {
	return toInt<int64_t>(v, what, "int64_t");
}

/** Calls f<int64_t> for x in int64_t's range and f<uint64_t> from 2**63 to 2**64 - 1, as C++ would for each type. */
template <typename F>
bool intOrUint64(const py::int_& x, const char* what, F f) {
	int overflow = 0;
	long long v = PyLong_AsLongLongAndOverflow(x.ptr(), &overflow);
	if (v == -1 && PyErr_Occurred())
		throw py::error_already_set();
	if (!overflow)
		return f((int64_t) v);
	if (overflow < 0)
		raiseOverflow(what, "int64_t");
	unsigned long long u = PyLong_AsUnsignedLongLong(x.ptr());
	if (u == (unsigned long long) -1 && PyErr_Occurred()) {
		PyErr_Clear();
		raiseOverflow(what, "uint64_t");
	}
	return f((uint64_t) u);
}

int positive(const py::int_& v, const char* what) {
	int b = cint(v, what);
	if (b <= 0)
		throw py::value_error(std::string(what) + ": b must be positive (Rack's precondition)");
	return b;
}

std::string vecRepr(const Vec& v) {
	return "Vec(" + floatRepr(v.x) + ", " + floatRepr(v.y) + ")";
}

int vecIndex(int i) {
	if (i != 0 && i != 1)
		throw py::index_error("Vec index must be 0 (x) or 1 (y)");
	return i;
}

}  // namespace

void bindMath(py::module_ raw) {
	py::module_ m = raw.def_submodule("math", "rack::math: scalar helpers and the 2D Vec and Rect types (math.hpp).");

	// --- integers -----------------------------------------------------------------
	m.def("is_even", [](const py::int_& x) {
		return intOrUint64(x, "is_even", [](auto v) { return rack::math::isEven(v); });
	}, py::arg("x"), "True if x is even (isEven<T>: T = int64_t, or uint64_t from 2**63 to 2**64 - 1).");
	m.def("is_odd", [](const py::int_& x) {
		return intOrUint64(x, "is_odd", [](auto v) { return rack::math::isOdd(v); });
	}, py::arg("x"), "True if x is odd (isOdd<T>: T = int64_t, or uint64_t from 2**63 to 2**64 - 1).");
	m.def("clamp", [](const py::int_& x, const py::int_& a, const py::int_& b) {
		return rack::math::clamp(cint(x, "clamp"), cint(a, "clamp"), cint(b, "clamp"));
	}, py::arg("x"), py::arg("a"), py::arg("b"), "Limits x between a and b; if b < a, returns a (int overload).");
	m.def("clamp_safe", [](const py::int_& x, const py::int_& a, const py::int_& b) {
		return rack::math::clampSafe(cint(x, "clamp_safe"), cint(a, "clamp_safe"), cint(b, "clamp_safe"));
	}, py::arg("x"), py::arg("a"), py::arg("b"), "Limits x between a and b, swapping them if b < a (int overload).");
	m.def("euc_mod", [](const py::int_& a, const py::int_& b) {
		return rack::math::eucMod(cint(a, "euc_mod"), positive(b, "euc_mod"));
	}, py::arg("a"), py::arg("b"), "Euclidean modulus, 0 <= result < b. b must be positive (int overload).");
	m.def("euc_div", [](const py::int_& a, const py::int_& b) {
		return rack::math::eucDiv(cint(a, "euc_div"), positive(b, "euc_div"));
	}, py::arg("a"), py::arg("b"), "Euclidean division. b must be positive.");
	m.def("euc_div_mod", [](const py::int_& a, const py::int_& b) {
		int div = 0, mod = 0;
		rack::math::eucDivMod(cint(a, "euc_div_mod"), positive(b, "euc_div_mod"), &div, &mod);
		return py::make_tuple(div, mod);
	}, py::arg("a"), py::arg("b"), "Euclidean division and modulus as (div, mod). b must be positive.");
	m.def("log2", [](const py::int_& n) {
		int v = cint(n, "log2");
		// Rack's loop never ends for a negative n (arithmetic shift), so refuse one.
		if (v < 0)
			throw py::value_error("log2: n must not be negative");
		return rack::math::log2(v);
	}, py::arg("n"), "floor(log2(n)); 0 for n == 0 or 1. n must not be negative.");
	m.def("is_pow2", [](const py::int_& n) {
		return intOrUint64(n, "is_pow2", [](auto v) { return rack::math::isPow2(v); });
	}, py::arg("n"), "True if n is a power of 2 (isPow2<T>: T = int64_t, or uint64_t from 2**63 to 2**64 - 1).");
	m.def("sgn", [](const py::int_& x) { return rack::math::sgn<int64_t>(i64(x, "sgn")); }, py::arg("x"),
		"1, -1 or 0 by the sign of x (sgn<T>, T = int64_t).");

	// --- floats -------------------------------------------------------------------
	m.def("sgn", [](double x, const std::string& type) {
		if (type == "float")
			return (double) rack::math::sgn<float>((float) x);
		if (type == "double")
			return rack::math::sgn<double>(x);
		throw py::value_error("sgn: type must be \"float\" or \"double\" (sgn<T>'s T for a Python float)");
	}, py::arg("x"), py::arg("type") = "float",
		"1.0, -1.0 or 0.0 by the sign of x (sgn<T>, T = float, or double with type=\"double\"). As a C++ float, a "
		"nonzero x too small for float has sign 0.");
	m.def("clamp", [](float x, float a, float b) { return rack::math::clamp(x, a, b); }, py::arg("x"), py::arg("a") = 0.f,
		py::arg("b") = 1.f, "Limits x between a and b (default 0 and 1); if b < a, returns a (float overload).");
	m.def("clamp_safe", [](float x, float a, float b) { return rack::math::clampSafe(x, a, b); }, py::arg("x"),
		py::arg("a") = 0.f, py::arg("b") = 1.f, "Limits x between a and b, swapping them if b < a (float overload).");
	m.def("normalize_zero", &rack::math::normalizeZero, py::arg("x"), "Converts -0.0 to 0.0; other values are unchanged.");
	m.def("euc_mod", [](float a, float b) { return rack::math::eucMod(a, b); }, py::arg("a"), py::arg("b"),
		"Euclidean modulus with fmod semantics (float overload); b = 0 gives NaN, as in C++.");
	m.def("is_near", &rack::math::isNear, py::arg("a"), py::arg("b"), py::arg("epsilon") = 1e-6f,
		"True if |a - b| <= epsilon.");
	m.def("chop", &rack::math::chop, py::arg("x"), py::arg("epsilon") = 1e-6f, "0 if |x| <= epsilon, else x.");
	m.def("rescale", &rack::math::rescale, py::arg("x"), py::arg("x_min"), py::arg("x_max"), py::arg("y_min"),
		py::arg("y_max"), "Rescales x from [x_min, x_max] to [y_min, y_max].");
	m.def("crossfade", [](float a, float b, float p) { return rack::math::crossfade(a, b, p); }, py::arg("a"), py::arg("b"),
		py::arg("p"), "Linear interpolation from a (p = 0) to b (p = 1).");
	m.def("interpolate_linear", [](const std::vector<float>& p, float x) {
		// Rack only documents the bound; check it, in float space before any conversion to int
		// (which is undefined for large x), so a bad index cannot read past the array.
		if (p.size() < 2 || !(x >= 0.f) || !(x < (float) (p.size() - 1)))
			throw py::index_error("interpolate_linear: p must have at least floor(x) + 2 values, and x must be >= 0");
		return rack::math::interpolateLinear(p.data(), x);
	}, py::arg("p"), py::arg("x"), "Linearly interpolates the sequence p at index x. p is copied; it is not modified.");
	m.def("complex_mult", [](float ar, float ai, float br, float bi) {
		float cr = 0.f, ci = 0.f;
		rack::math::complexMult(ar, ai, br, bi, &cr, &ci);
		return py::make_tuple(cr, ci);
	}, py::arg("ar"), py::arg("ai"), py::arg("br"), py::arg("bi"), "Complex product (ar + ai*i)(br + bi*i) as (cr, ci).");

	// --- Vec ----------------------------------------------------------------------
	py::class_<Vec> vec(m, "Vec", "A 2D vector of C++ floats (math::Vec). A mutable value: assignment copies it, "
		"but a Vec read from a Rect field is a live view of that field.");
	py::class_<Rect> rect(m, "Rect", "A 2D rectangle (math::Rect): includes its left/top edge but not its right/bottom edge.");

	vec.def(py::init<>())
		.def(py::init<float>(), py::arg("xy"))
		.def(py::init<float, float>(), py::arg("x"), py::arg("y"))
		.def_readwrite("x", &Vec::x)
		.def_readwrite("y", &Vec::y)
		.def("__getitem__", [](const Vec& v, int i) { return static_cast<const Vec&>(v)[vecIndex(i)]; })
		.def("__setitem__", [](Vec& v, int i, float value) { v[vecIndex(i)] = value; })
		.def("neg", &Vec::neg)
		.def("plus", &Vec::plus, py::arg("b"))
		.def("minus", &Vec::minus, py::arg("b"))
		.def("mult", py::overload_cast<float>(&Vec::mult, py::const_), py::arg("s"))
		.def("mult", py::overload_cast<Vec>(&Vec::mult, py::const_), py::arg("b"))
		.def("div", py::overload_cast<float>(&Vec::div, py::const_), py::arg("s"))
		.def("div", py::overload_cast<Vec>(&Vec::div, py::const_), py::arg("b"))
		.def("dot", &Vec::dot, py::arg("b"))
		.def("arg", &Vec::arg)
		.def("norm", &Vec::norm)
		.def("normalize", &Vec::normalize)
		.def("square", &Vec::square)
		.def("area", &Vec::area)
		.def("rotate", &Vec::rotate, py::arg("angle"), "Rotates counterclockwise by angle radians; returns a new Vec.")
		.def("flip", &Vec::flip)
		.def("min", &Vec::min, py::arg("b"))
		.def("max", &Vec::max, py::arg("b"))
		.def("abs", &Vec::abs)
		.def("round", &Vec::round)
		.def("floor", &Vec::floor)
		.def("ceil", &Vec::ceil)
		.def("equals", &Vec::equals, py::arg("b"))
		.def("is_zero", &Vec::isZero)
		.def("is_finite", &Vec::isFinite)
		.def("clamp", &Vec::clamp, py::arg("bound"))
		.def("clamp_safe", &Vec::clampSafe, py::arg("bound"))
		.def("crossfade", &Vec::crossfade, py::arg("b"), py::arg("p"))
		.def("is_equal", &Vec::isEqual, py::arg("b"))
		// Rack's free operator overloads for Vec.
		.def("__pos__", [](const Vec& a) { return rack::math::operator+(a); })
		.def("__neg__", [](const Vec& a) { return rack::math::operator-(a); })
		.def("__add__", [](const Vec& a, const Vec& b) { return rack::math::operator+(a, b); }, py::is_operator())
		.def("__sub__", [](const Vec& a, const Vec& b) { return rack::math::operator-(a, b); }, py::is_operator())
		.def("__mul__", [](const Vec& a, const Vec& b) { return rack::math::operator*(a, b); }, py::is_operator())
		.def("__mul__", [](const Vec& a, float b) { return rack::math::operator*(a, b); }, py::is_operator())
		.def("__rmul__", [](const Vec& b, float a) { return rack::math::operator*(a, b); }, py::is_operator())
		.def("__truediv__", [](const Vec& a, const Vec& b) { return rack::math::operator/(a, b); }, py::is_operator())
		.def("__truediv__", [](const Vec& a, float b) { return rack::math::operator/(a, b); }, py::is_operator())
		.def("__iadd__", [](py::object self, const Vec& b) { rack::math::operator+=(self.cast<Vec&>(), b); return self; }, py::is_operator())
		.def("__isub__", [](py::object self, const Vec& b) { rack::math::operator-=(self.cast<Vec&>(), b); return self; }, py::is_operator())
		.def("__imul__", [](py::object self, const Vec& b) { rack::math::operator*=(self.cast<Vec&>(), b); return self; }, py::is_operator())
		.def("__imul__", [](py::object self, float b) { rack::math::operator*=(self.cast<Vec&>(), b); return self; }, py::is_operator())
		.def("__itruediv__", [](py::object self, const Vec& b) { rack::math::operator/=(self.cast<Vec&>(), b); return self; }, py::is_operator())
		.def("__itruediv__", [](py::object self, float b) { rack::math::operator/=(self.cast<Vec&>(), b); return self; }, py::is_operator())
		.def("__eq__", [](const Vec& a, const Vec& b) { return rack::math::operator==(a, b); }, py::is_operator())
		.def("__ne__", [](const Vec& a, const Vec& b) { return rack::math::operator!=(a, b); }, py::is_operator())
		// Python conventions: a mutable value is unhashable; repr shows the exact float values.
		.def("__repr__", &vecRepr)
		.def("__iter__", [](const Vec& v) { return py::iter(py::make_tuple(v.x, v.y)); })
		.def("__len__", [](const Vec&) { return 2; })
		.def("__copy__", [](const Vec& v) { return v; })
		.def("__deepcopy__", [](const Vec& v, py::dict) { return v; }, py::arg("memo"));
	vec.attr("__hash__") = py::none();

	rect.def(py::init<>())
		.def(py::init<Vec, Vec>(), py::arg("pos"), py::arg("size"))
		.def(py::init<float, float, float, float>(), py::arg("pos_x"), py::arg("pos_y"), py::arg("size_x"), py::arg("size_y"))
		// Fields are live views: r.pos.x = 1 changes r, as in C++. The view keeps r alive.
		.def_readwrite("pos", &Rect::pos)
		.def_readwrite("size", &Rect::size)
		.def_static("from_min_max", &Rect::fromMinMax, py::arg("a"), py::arg("b"))
		.def_static("from_corners", &Rect::fromCorners, py::arg("a"), py::arg("b"))
		.def_static("inf", &Rect::inf, "The infinite Rect: pos = -inf, size = inf.")
		.def("contains", py::overload_cast<Vec>(&Rect::contains, py::const_), py::arg("v"))
		.def("contains", py::overload_cast<Rect>(&Rect::contains, py::const_), py::arg("r"))
		.def("intersects", &Rect::intersects, py::arg("r"))
		.def("equals", &Rect::equals, py::arg("r"))
		.def("get_left", &Rect::getLeft)
		.def("get_right", &Rect::getRight)
		.def("get_top", &Rect::getTop)
		.def("get_bottom", &Rect::getBottom)
		.def("get_width", &Rect::getWidth)
		.def("get_height", &Rect::getHeight)
		.def("get_center", &Rect::getCenter)
		.def("get_top_left", &Rect::getTopLeft)
		.def("get_top_right", &Rect::getTopRight)
		.def("get_bottom_left", &Rect::getBottomLeft)
		.def("get_bottom_right", &Rect::getBottomRight)
		.def("clamp", &Rect::clamp, py::arg("bound"))
		.def("nudge", &Rect::nudge, py::arg("bound"))
		.def("expand", &Rect::expand, py::arg("b"))
		.def("intersect", &Rect::intersect, py::arg("b"))
		.def("zero_pos", &Rect::zeroPos)
		.def("grow", &Rect::grow, py::arg("delta"))
		.def("shrink", &Rect::shrink, py::arg("delta"))
		.def("interpolate", &Rect::interpolate, py::arg("p"))
		.def("is_containing", &Rect::isContaining, py::arg("v"))
		.def("is_intersecting", &Rect::isIntersecting, py::arg("r"))
		.def("is_equal", &Rect::isEqual, py::arg("r"))
		.def("__eq__", [](const Rect& a, const Rect& b) { return rack::math::operator==(a, b); }, py::is_operator())
		.def("__ne__", [](const Rect& a, const Rect& b) { return rack::math::operator!=(a, b); }, py::is_operator())
		// Facade conveniences (rackpy.math): read-only properties over the getters above.
		.def_property_readonly("left", &Rect::getLeft)
		.def_property_readonly("right", &Rect::getRight)
		.def_property_readonly("top", &Rect::getTop)
		.def_property_readonly("bottom", &Rect::getBottom)
		.def_property_readonly("width", &Rect::getWidth)
		.def_property_readonly("height", &Rect::getHeight)
		.def_property_readonly("center", &Rect::getCenter)
		.def("__repr__", [](const Rect& r) {
			return "Rect(" + floatRepr(r.pos.x) + ", " + floatRepr(r.pos.y) + ", " + floatRepr(r.size.x) + ", " + floatRepr(r.size.y) + ")";
		})
		.def("__copy__", [](const Rect& r) { return r; })
		.def("__deepcopy__", [](const Rect& r, py::dict) { return r; }, py::arg("memo"));
	rect.attr("__hash__") = py::none();
}

}  // namespace values
}  // namespace rackpy
