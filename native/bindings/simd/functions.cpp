// SPDX-License-Identifier: GPL-3.0-or-later
// rack::simd functions (functions.hpp) -> rackpy.raw.simd.
//
// Each name has Rack's float_4 overload and, as in C++, the scalar functions
// functions.hpp imports with `using std::sin;` and `using math::clamp;`, so a
// generic function written once works for floats and vectors, as Rack
// intends:
//
//     def sin_plus_cos(x):  # float or float_4
//         return simd.sin(x) + simd.cos(x)
//
// The scalar overloads are bound for T = float (single precision, as in C++
// templates instantiated with float); a Python float is rounded to a C++ float.
// Scalar overloads are registered first: a Python float or int picks the
// scalar function, a float_4 the vector one, and a scalar mixed with a float_4
// broadcasts.
#include "simd.hpp"

#include <pybind11/complex.h>

#include <cmath>
#include <string>

#include "../values/values.hpp"

namespace rackpy {
namespace simd_bindings {

namespace s = rack::simd;
using rack::simd::float_4;
using rack::simd::int32_4;

namespace {

/** Rack's pow<T>(T, int) doubles a C int from 1 while it is <= b: for b >= 2**30 that overflows,
which is undefined behaviour and in practice never ends. A negative b gives 1, as in Rack. */
int exponent(const py::int_& b) {
	int e = values::toInt<int>(b, "pow", "int");
	if (e >= (1 << 30))
		throw py::value_error("pow: an int exponent must be below 2**30 (Rack's loop overflows for larger ones)");
	return e;
}

}  // namespace

void bindFunctions(py::module_ m) {
	// --- Functions based on instructions -------------------------------------------
	m.def("andnot", [](const float_4& a, const float_4& b) { return s::andnot(a, b); }, py::arg("a"), py::arg("b"),
		"~a & b, bitwise.");
	m.def("movemask", [](const float_4& a) { return s::movemask(a); }, py::arg("a"),
		"An int whose bit k is the sign (most significant) bit of lane k: movemask(float_4.mask()) == 0xf.");
	m.def("movemask", [](const int32_4& a) { return s::movemask(a); }, py::arg("a"),
		"An int whose bit k is the sign (most significant) bit of lane k: movemask(int32_4.mask()) == 0xf.");
	m.def("rsqrt", [](const float_4& x) { return s::rsqrt(x); }, py::arg("x"),
		"The CPU's approximate 1/sqrt(x) (_mm_rsqrt_ps; relative error up to 1.5 * 2**-12 on x86).");
	m.def("rcp", [](const float_4& x) { return s::rcp(x); }, py::arg("x"),
		"The CPU's approximate 1/x (_mm_rcp_ps; relative error up to 1.5 * 2**-12 on x86).");

	// --- Nonstandard convenience functions -----------------------------------------
	m.def("ifelse", [](bool cond, float a, float b) { return s::ifelse(cond, a, b); }, py::arg("cond"), py::arg("a"),
		py::arg("b"), "a if cond else b (the scalar overload).");
	m.def("ifelse", [](const float_4& mask, const float_4& a, const float_4& b) { return s::ifelse(mask, a, b); },
		py::arg("mask"), py::arg("a"), py::arg("b"),
		"Per lane, a where mask is all ones and b where it is all zeros: (a & mask) | andnot(mask, b), bit by bit.");
	py::dict inverse;
	inverse[py::make_tuple(py::type::of<int32_4>())] = py::cpp_function(
		[](const py::int_& a) { return s::movemaskInverse<int32_4>(values::toInt<int>(a, "movemask_inverse", "int")); },
		py::name("movemask_inverse[int32_4]"), py::arg("a"), "Lane k is all ones if bit k of a is 1, else all zeros.");
	inverse[py::make_tuple(py::type::of<float_4>())] = py::cpp_function(
		[](const py::int_& a) { return s::movemaskInverse<float_4>(values::toInt<int>(a, "movemask_inverse", "int")); },
		py::name("movemask_inverse[float_4]"), py::arg("a"), "Lane k is all ones if bit k of a is 1, else all zeros.");
	addTemplate(m, "movemask_inverse", inverse);

	// --- Standard math functions ---------------------------------------------------
#define RACKPY_UNARY(name, doc) \
	m.def(#name, [](float x) { return s::name(x); }, py::arg("x"), "std::" #name " for a float (simd imports it)."); \
	m.def(#name, [](const float_4& x) { return s::name(x); }, py::arg("x"), doc);
#define RACKPY_BINARY(name, a, b, doc) \
	m.def(#name, [](float x, float y) { return s::name(x, y); }, py::arg(#a), py::arg(#b), "std::" #name " for floats (simd imports it)."); \
	m.def(#name, [](const float_4& x, const float_4& y) { return s::name(x, y); }, py::arg(#a), py::arg(#b), doc);

	RACKPY_BINARY(fmax, x, b, "Per-lane maximum (_mm_max_ps: where either lane is NaN, the result is b's lane).")
	RACKPY_BINARY(fmin, x, b, "Per-lane minimum (_mm_min_ps: where either lane is NaN, the result is b's lane).")
	RACKPY_UNARY(sqrt, "Per-lane square root (_mm_sqrt_ps).")
	RACKPY_UNARY(log, "Natural logarithm (sse_mathfun's approximation); NaN for x <= 0, zero included.")
	RACKPY_UNARY(log10, "log(x) / log(10), with sse_mathfun's log.")
	RACKPY_UNARY(log2, "log(x) / log(2), with sse_mathfun's log.")
	RACKPY_UNARY(exp, "e**x (sse_mathfun's approximation); saturates at about 2.406e38, also returned for inf and NaN.")
	RACKPY_UNARY(sin, "Sine (sse_mathfun's approximation).")
	RACKPY_UNARY(cos, "Cosine (sse_mathfun's approximation).")
	RACKPY_UNARY(tan, "Tangent (sse_mathfun's approximation).")
	RACKPY_UNARY(atan, "Arctangent (sse_mathfun's approximation).")
	RACKPY_BINARY(atan2, x, y, "As std::atan2(x, y): the angle of the point (y, x). Rack names the numerator x (sse_mathfun's approximation).")
	RACKPY_UNARY(trunc, "Rounds each lane toward zero.")
	RACKPY_UNARY(floor, "Rounds each lane down.")
	RACKPY_UNARY(ceil, "Rounds each lane up.")
	RACKPY_UNARY(round, "Rounds each lane to the nearest integer, ties to even (_mm_round_ps; std::round rounds ties away).")
	RACKPY_BINARY(fmod, a, b, "a - floor(a / b) * b: Rack's floored modulus, with the sign of b (std::fmod truncates).")
	RACKPY_BINARY(hypot, a, b, "sqrt(a * a + b * b), without std::hypot's overflow protection.")
	RACKPY_UNARY(fabs, "Clears each lane's sign bit.")
#undef RACKPY_UNARY
#undef RACKPY_BINARY

	m.def("abs", [](float x) { return s::abs(x); }, py::arg("x"), "std::abs for a float (simd imports it).");
	m.def("abs", [](std::complex<double> z) { return s::abs(std::complex<float>((float) z.real(), (float) z.imag())); },
		py::arg("z"), "std::abs of a std::complex<float>: a Python complex, rounded to float.");
	m.def("abs", [](const float_4& a) { return s::abs(a); }, py::arg("a"), "Clears each lane's sign bit (fabs).");
	m.def("abs", [](const std::complex<float_4>& a) { return s::abs(a); }, py::arg("a"),
		"The magnitude of a std::complex<float_4>, given as (real, imag): hypot(real, imag).");
	m.def("arg", [](std::complex<double> z) { return s::arg(std::complex<float>((float) z.real(), (float) z.imag())); },
		py::arg("z"), "std::arg of a std::complex<float>: a Python complex, rounded to float.");
	m.def("arg", [](const std::complex<float_4>& a) { return s::arg(a); }, py::arg("a"),
		"The angle of a std::complex<float_4>, given as (real, imag): atan2(imag, real).");

	// pow: Rack's pow<T>(T, int) for an int exponent, std::pow for float ones, and Rack's vector overloads.
	m.def("pow", [](float a, const py::int_& b) { return s::pow<float>(a, exponent(b)); }, py::arg("a"), py::arg("b"),
		"Rack's pow<T>(T, int) with T = float: a**b by squaring; 1 for b <= 0. b must be below 2**30.");
	m.def("pow", [](float a, float b) { return s::pow(a, b); }, py::arg("a"), py::arg("b"), "std::pow for floats.");
	m.def("pow", [](const float_4& a, const py::int_& b) { return s::pow<float_4>(a, exponent(b)); }, py::arg("a"),
		py::arg("b"), "Rack's pow<T>(T, int) with T = float_4: a**b by squaring; 1 for b <= 0. b must be below 2**30.");
	m.def("pow", [](float a, const float_4& b) { return s::pow(a, b); }, py::arg("a"), py::arg("b"),
		"exp(b * log(a)), with std::log for the scalar a; about 2.406e38 where a <= 0.");
	m.def("pow", [](const float_4& a, const float_4& b) { return s::pow(a, b); }, py::arg("a"), py::arg("b"),
		"exp(b * log(a)): where a <= 0, exp of NaN, about 2.406e38.");

	// --- From math.hpp -------------------------------------------------------------
	m.def("clamp", [](float x, float a, float b) { return s::clamp(x, a, b); }, py::arg("x"), py::arg("a") = 0.f,
		py::arg("b") = 1.f, "math::clamp for floats (simd imports it).");
	m.def("clamp", [](const float_4& x, const float_4& a, const float_4& b) { return s::clamp(x, a, b); }, py::arg("x"),
		py::arg("a") = float_4(0.f), py::arg("b") = float_4(1.f), "fmin(fmax(x, a), b) per lane: a wins if b < a.");
	m.def("rescale", [](float x, float x_min, float x_max, float y_min, float y_max) {
		return s::rescale(x, x_min, x_max, y_min, y_max);
	}, py::arg("x"), py::arg("x_min"), py::arg("x_max"), py::arg("y_min"), py::arg("y_max"),
		"math::rescale for floats (simd imports it).");
	m.def("rescale", [](const float_4& x, const float_4& x_min, const float_4& x_max, const float_4& y_min, const float_4& y_max) {
		return s::rescale(x, x_min, x_max, y_min, y_max);
	}, py::arg("x"), py::arg("x_min"), py::arg("x_max"), py::arg("y_min"), py::arg("y_max"),
		"Rescales x from [x_min, x_max] to [y_min, y_max], per lane.");
	m.def("crossfade", [](float a, float b, float p) { return s::crossfade(a, b, p); }, py::arg("a"), py::arg("b"), py::arg("p"),
		"math::crossfade for floats (simd imports it).");
	m.def("crossfade", [](const float_4& a, const float_4& b, const float_4& p) { return s::crossfade(a, b, p); },
		py::arg("a"), py::arg("b"), py::arg("p"), "a + (b - a) * p, per lane.");
	m.def("sgn", [](float x) { return s::sgn(x); }, py::arg("x"), "math::sgn<float> (simd imports it).");
	m.def("sgn", [](const float_4& x) { return s::sgn(x); }, py::arg("x"),
		"Per lane, 1 or -1 by the sign bit, and 0 for zero; NaN lanes give +-1.");
}

}  // namespace simd_bindings
}  // namespace rackpy
