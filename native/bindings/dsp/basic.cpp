// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/common.hpp, approx.hpp, convert.hpp, window.hpp and ode.hpp -> rackpy.raw.dsp (RP-18A).
//
// Every function calls Rack's own inline code, compiled with the SDK's flags
// (tests/dsp compares results with a compiled C++ reference inside Rack).
// Element-type templates are instantiated for T = float and T = simd::float_4,
// the float overload first, so a float picks the scalar instantiation, a
// float_4 the vector one, and a scalar mixed with a float_4 broadcasts, as in
// rackpy.raw.simd. Templates over a compile-time integer (the coefficient count
// N of the poly* functions) are instantiated for a bounded range and refuse
// larger values (docs/reference/dsp.md#dsp-templates). Frame<CHANNELS, T> is
// plain data; it is in src/rackpy/raw/_dsp.py.
//
// Where Rack's code is undefined for some input (exp2Floor<float> outside its
// domain, a stepper's stack arrays), RackPy refuses the input first. Every Rack
// function is called through calls.hpp, out of line, as the C++ reference is.
#include "dsp.hpp"

#include <pybind11/complex.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../simd/simd.hpp"
#include "calls.hpp"
#include "../support/overrides.hpp"
#include "../values/values.hpp"

namespace rackpy {
namespace dsp_bindings {

namespace d = rack::dsp;
using rack::simd::float_4;

namespace {

// --- approx.hpp -------------------------------------------------------------------

/** Rack's exp2Floor<float> converts x + 127 to int32_t and shifts it left by 23 bits. That is
undefined unless the sum, in float, is above -1 (a negative int would be shifted) and below 256
(the shift would overflow). The float_4 specialization uses SSE conversions and shifts, which are
defined for every input, so only the float one is checked. */
void exp2Domain(float x, const char* what) {
	float y = x + 127;
	if (!(y > -1.f && y < 256.f))
		throw py::value_error(std::string(what) +
			": x + 127 must be above -1 and below 256 for a float; Rack's code is undefined outside that");
}

namespace c = dsp_calls;
using c::Poly;
using c::polyN;

template <Poly P, typename T, size_t... I>
std::array<T (*)(const T*, T), sizeof...(I)> polyTable(std::index_sequence<I...>) {
	return {{&polyN<P, T, I + 1>...}};
}

/** polyDirect/Horner/Estrin<T, N> with N = a.size(): one instantiation per N from 1 to MAX_POLY_COEFFICIENTS.
C++ has no zero-length arrays, so N = 0 does not exist. */
template <Poly P, typename T>
T poly(const std::vector<T>& a, T x, const char* what) {
	static const auto table = polyTable<P, T>(std::make_index_sequence<MAX_POLY_COEFFICIENTS>());
	if (a.empty() || a.size() > MAX_POLY_COEFFICIENTS)
		throw py::value_error(std::string(what) + ": the coefficient count N must be 1 to " +
			std::to_string(MAX_POLY_COEFFICIENTS) + " (RackPy instantiates Rack's template for those), not " +
			std::to_string(a.size()));
	return table[a.size() - 1](a.data(), x);
}

// --- convert.hpp ------------------------------------------------------------------

template <typename T>
struct Sample;  // how a sample type crosses into and out of Python, and its name

template <>
struct Sample<float> {
	static constexpr const char* name = "float";
	static float in(py::handle h) { return h.cast<float>(); }
	static py::object out(float v) { return py::float_(v); }
	static bool matches(const py::buffer_info& info) { return info.item_type_is_equivalent_to<float>(); }
	static constexpr size_t bytes = sizeof(float);
};

constexpr const char* intName(int8_t) { return "int8_t"; }
constexpr const char* intName(int16_t) { return "int16_t"; }
constexpr const char* intName(int32_t) { return "int32_t"; }
constexpr const char* intName(int64_t) { return "int64_t"; }

template <typename I>
struct IntSample {
	static constexpr const char* name = intName(I{});
	static I in(py::handle h) {
		if (!py::isinstance<py::int_>(h))
			throw py::type_error(std::string("convert: an ") + name + " sample needs a Python int, not " +
				py::repr(h).cast<std::string>());
		return values::toInt<I>(h.cast<py::int_>(), "convert", name);
	}
	static py::object out(I v) { return py::int_((long long) v); }
	static bool matches(const py::buffer_info& info) { return info.item_type_is_equivalent_to<I>(); }
	static constexpr size_t bytes = sizeof(I);
};
template <>
struct Sample<int8_t> : IntSample<int8_t> {};
template <>
struct Sample<int16_t> : IntSample<int16_t> {};
template <>
struct Sample<int32_t> : IntSample<int32_t> {};
template <>
struct Sample<int64_t> : IntSample<int64_t> {};

template <>
struct Sample<d::Int24> {
	static constexpr const char* name = "Int24";
	static d::Int24 in(py::handle h) {
		if (py::isinstance<py::int_>(h))
			return d::Int24(values::toInt<int32_t>(h.cast<py::int_>(), "convert", "int32_t"));
		return h.cast<d::Int24>();
	}
	static py::object out(d::Int24 v) { return py::cast(v); }
	// Int24 is 3 packed bytes: any buffer of single bytes, 3 per sample.
	static bool matches(const py::buffer_info& info) { return info.itemsize == 1; }
	static constexpr size_t bytes = 3;
};
static_assert(sizeof(d::Int24) == 3, "Int24 must be 3 bytes, as Rack asserts");

/** A contiguous 1-D buffer of `count` samples of T, writable if asked. */
template <typename T>
void* samples(const py::buffer& buf, bool writable, size_t count, const char* what) {
	py::buffer_info info = buf.request(writable);
	if (!Sample<T>::matches(info))
		throw py::type_error(std::string("convert: ") + what + " needs a buffer of " + Sample<T>::name +
			(std::is_same<T, d::Int24>::value ? " (3 bytes per sample, as single bytes)" : "") + " items, not '" +
			info.format + "'");
	if (info.ndim != 1 || info.strides[0] != info.itemsize)
		throw py::value_error(std::string("convert: ") + what + " needs a contiguous one-dimensional buffer");
	if ((size_t) info.size * (size_t) info.itemsize < count * Sample<T>::bytes)
		throw py::value_error(std::string("convert: ") + what + " is shorter than len samples");
	return info.ptr;
}

template <typename To, typename From>
py::cpp_function convertFunction() {
	std::string name = std::string("convert[") + Sample<To>::name + ", " + Sample<From>::name + "]";
	py::cpp_function scalar(
		[](py::object x) { return Sample<To>::out(c::convert<To, From>(Sample<From>::in(x))); }, py::name(name.c_str()),
		py::arg("x"), "Rack's convert<To, From>(x) for one sample.");
	return py::cpp_function(
		[](py::buffer in, py::buffer out, const py::int_& len) {
			size_t n = values::toInt<uint32_t>(len, "convert", "size_t (at most 2**32 - 1 here)");
			const From* src = static_cast<const From*>(samples<From>(in, false, n, "in"));
			To* dst = static_cast<To*>(samples<To>(out, true, n, "out"));
			const char* a = reinterpret_cast<const char*>(src);
			const char* b = reinterpret_cast<const char*>(dst);
			// Rack's loop reads in[i] and writes out[i] through two pointer types: if the arrays overlap
			// and the types differ, that is undefined (strict aliasing), so refuse it.
			if (!std::is_same<To, From>::value && a < b + n * Sample<To>::bytes && b < a + n * Sample<From>::bytes)
				throw py::value_error("convert: in and out overlap; Rack's loop needs separate arrays for different types");
			c::convertBuffer<To, From>(src, dst, n);
		},
		py::name(name.c_str()), py::arg("in_"), py::arg("out"), py::arg("len"), py::sibling(scalar),
		"Rack's convert<To, From>(in, out, len): converts len samples from one buffer into another.");
}

// --- window.hpp -------------------------------------------------------------------

float* windowBuffer(const py::buffer& x, int len, const char* what) {
	py::buffer_info info = x.request(true);
	if (!info.item_type_is_equivalent_to<float>())
		throw py::type_error(std::string(what) + " needs a writable buffer of float32 ('f') items, not '" + info.format + "'");
	if (info.ndim != 1 || info.strides[0] != (py::ssize_t) sizeof(float))
		throw py::value_error(std::string(what) + " needs a contiguous one-dimensional buffer");
	if (len > info.size)
		throw py::value_error(std::string(what) + ": len is " + std::to_string(len) + " but the buffer has " +
			std::to_string(info.size) + " items");
	return static_cast<float*>(info.ptr);
}

// --- ode.hpp ----------------------------------------------------------------------

using c::Method;

template <typename T>
void step(Method method, T t, T dt, T* x, int len, c::OdeSystem<T> f, void* context) {
	switch (method) {
		case Method::Euler: c::step<Method::Euler, T>(t, dt, x, len, f, context); break;
		case Method::RK2: c::step<Method::RK2, T>(t, dt, x, len, f, context); break;
		case Method::RK4: c::step<Method::RK4, T>(t, dt, x, len, f, context); break;
	}
}

void odeLen(int len, py::ssize_t available, const char* what) {
	// Rack's steppers declare len-sized arrays on the stack: len < 1 is undefined, and a large one
	// overflows the stack.
	if (len < 1 || len > MAX_ODE_LEN)
		throw py::value_error(std::string(what) + ": len must be 1 to " + std::to_string(MAX_ODE_LEN) + ", not " +
			std::to_string(len));
	if (len > available)
		throw py::value_error(std::string(what) + ": len is " + std::to_string(len) + " but x has " +
			std::to_string(available) + " values");
}

/** The Python system and what its trampoline needs. */
struct PythonSystem {
	const py::function& f;
	int len;
	const char* what;
};

/** f(t, x, dxdt) for T = float: x as a read-only copy, dxdt as a zeroed array('f') to fill. Rack's
stack arrays never reach Python. Python errors propagate through Rack's stepper as C++ exceptions. */
void floatSystem(void* context, float t, const float* x, float* dxdt) {
	auto& system = *static_cast<PythonSystem*>(context);
	size_t bytes = (size_t) system.len * sizeof(float);
	py::object array = py::module_::import("array").attr("array");
	py::object xs = py::memoryview(array("f", py::bytes(reinterpret_cast<const char*>(x), bytes))).attr("toreadonly")();
	py::object out = array("f", py::bytes(std::string(bytes, '\0')));
	system.f(t, xs, out);
	py::buffer_info result = py::buffer(out).request();
	if (result.size < system.len || !result.item_type_is_equivalent_to<float>())
		throw py::value_error(std::string(system.what) + ": f must leave dxdt with len float32 values");
	std::memcpy(dxdt, result.ptr, bytes);
}

/** f(t, x, dxdt) for T = float_4: x as a tuple of copies, dxdt as a list of zero vectors to fill. */
void vectorSystem(void* context, float_4 t, const float_4* x, float_4* dxdt) {
	auto& system = *static_cast<PythonSystem*>(context);
	size_t n = (size_t) system.len;
	py::tuple xs(n);
	py::list out(n);
	for (size_t i = 0; i < n; i++) {
		xs[i] = py::cast(x[i]);
		out[i] = py::cast(float_4::zero());
	}
	system.f(t, xs, out);
	if (out.size() < n)
		throw py::value_error(std::string(system.what) + ": f must leave dxdt with len vectors");
	for (size_t i = 0; i < n; i++)
		dxdt[i] = out[i].cast<float_4>();
}

/** The stepper over a float32 buffer x, updated in place. */
void stepFloats(Method method, float t, float dt, const py::buffer& x, int len, const py::function& f, const char* what) {
	py::buffer_info info = x.request(true);
	if (!info.item_type_is_equivalent_to<float>())
		throw py::type_error(std::string(what) + ": x needs a writable buffer of float32 ('f') items, not '" + info.format + "'");
	if (info.ndim != 1 || info.strides[0] != (py::ssize_t) sizeof(float))
		throw py::value_error(std::string(what) + ": x needs a contiguous one-dimensional buffer");
	odeLen(len, info.size, what);
	PythonSystem system{f, len, what};
	step<float>(method, t, dt, static_cast<float*>(info.ptr), len, &floatSystem, &system);
}

/** The stepper over a list of float_4 x, whose vectors it updates in place. */
void stepVectors(Method method, const float_4& t, const float_4& dt, const py::list& x, int len, const py::function& f,
	const char* what) {
	odeLen(len, (py::ssize_t) x.size(), what);
	std::vector<float_4> state((size_t) len);
	for (int i = 0; i < len; i++)
		state[(size_t) i] = x[(size_t) i].cast<float_4>();
	PythonSystem system{f, len, what};
	step<float_4>(method, t, dt, state.data(), len, &vectorSystem, &system);
	// Write back into the caller's vectors, as Rack writes x[] in place.
	for (int i = 0; i < len; i++) {
		py::object item = x[(size_t) i];
		if (py::isinstance<float_4>(item))
			item.cast<float_4&>() = state[(size_t) i];
		else
			x[(size_t) i] = py::cast(state[(size_t) i]);
	}
}

void deprecatedAlias(const char* old, const char* current) {
	bindings::deprecated((std::string("rackpy.raw.dsp.") + old + " is deprecated in Rack; use " + current).c_str());
}

}  // namespace

void bindBasic(py::module_ m) {
	// --- common.hpp ---------------------------------------------------------------
	m.attr("FREQ_C4") = d::FREQ_C4;
	m.attr("FREQ_A4") = d::FREQ_A4;
	m.attr("FREQ_SEMITONE") = d::FREQ_SEMITONE;

	m.def("sinc", [](float x) { return c::sinc(x); }, py::arg("x"), "The normalized sinc, sin(pi x) / (pi x), with sinc(0) = 1.");
	m.def("sinc", [](const float_4& x) { return c::sinc(x); }, py::arg("x"), "sinc<T> for T = float_4.");

#define RACKPY_T1(pyname, fn, argname, doc) \
	m.def(pyname, [](float x) { return c::fn<float>(x); }, py::arg(argname), doc " (T = float)."); \
	m.def(pyname, [](const float_4& x) { return c::fn<float_4>(x); }, py::arg(argname), doc " (T = float_4).");

	RACKPY_T1("amplitude_to_db", amplitudeToDb, "amp", "20 log10(amp)")
	RACKPY_T1("db_to_amplitude", dbToAmplitude, "db", "10**(db / 20)")
	RACKPY_T1("quadratic_bipolar", quadraticBipolar, "x", "sgn(x) x**2")
	RACKPY_T1("cubic", cubic, "x", "x**3")
	RACKPY_T1("quartic_bipolar", quarticBipolar, "x", "sgn(x) x**4")
	RACKPY_T1("quintic", quintic, "x", "x**5")
	RACKPY_T1("sqrt_bipolar", sqrtBipolar, "x", "sgn(x) sqrt(x): NaN for x < 0, as in Rack")
	m.def("exponential_bipolar", [](float b, float x) { return c::exponentialBipolar<float>(b, x); }, py::arg("b"),
		py::arg("x"), "(b**x - b**-x) / (b - 1/b), a scaled sinh (T = float).");
	m.def("exponential_bipolar", [](const float_4& b, const float_4& x) { return c::exponentialBipolar<float_4>(b, x); },
		py::arg("b"), py::arg("x"), "(b**x - b**-x) / (b - 1/b), a scaled sinh (T = float_4).");

	// --- approx.hpp ---------------------------------------------------------------
	m.def("poly_direct", [](const std::vector<float>& a, float x) { return poly<Poly::Direct>(a, x, "poly_direct"); },
		py::arg("a"), py::arg("x"), "sum(a[n] x**n), evaluated directly (T = float; N = len(a), 1 to 32).");
	m.def("poly_direct", [](const std::vector<float_4>& a, const float_4& x) { return poly<Poly::Direct>(a, x, "poly_direct"); },
		py::arg("a"), py::arg("x"), "sum(a[n] x**n), evaluated directly (T = float_4; N = len(a), 1 to 32).");
	m.def("poly_horner", [](const std::vector<float>& a, float x) { return poly<Poly::Horner>(a, x, "poly_horner"); },
		py::arg("a"), py::arg("x"), "sum(a[n] x**n) by Horner's method (T = float; N = len(a), 1 to 32).");
	m.def("poly_horner", [](const std::vector<float_4>& a, const float_4& x) { return poly<Poly::Horner>(a, x, "poly_horner"); },
		py::arg("a"), py::arg("x"), "sum(a[n] x**n) by Horner's method (T = float_4; N = len(a), 1 to 32).");
	m.def("poly_estrin", [](const std::vector<float>& a, float x) { return poly<Poly::Estrin>(a, x, "poly_estrin"); },
		py::arg("a"), py::arg("x"), "sum(a[n] x**n) by Estrin's scheme (T = float; N = len(a), 1 to 32).");
	m.def("poly_estrin", [](const std::vector<float_4>& a, const float_4& x) { return poly<Poly::Estrin>(a, x, "poly_estrin"); },
		py::arg("a"), py::arg("x"), "sum(a[n] x**n) by Estrin's scheme (T = float_4; N = len(a), 1 to 32).");

	m.def("exp2_floor", [](float x) {
		exp2Domain(x, "exp2_floor");
		float xf = 0.f;
		float y = c::exp2Floor<float>(x, &xf);
		return py::make_tuple(y, xf);
	}, py::arg("x"), "(2**floor(x), the fractional part of x) for T = float; C++ returns the fraction through xf.");
	m.def("exp2_floor", [](const float_4& x) {
		float_4 xf = 0.f;
		float_4 y = c::exp2Floor<float_4>(x, &xf);
		return py::make_tuple(y, xf);
	}, py::arg("x"), "(2**floor(x), the fractional part of x) for T = float_4.");
	m.def("approx_exp2_floor", [](float x) {
		deprecatedAlias("approx_exp2_floor", "exp2_floor");
		exp2Domain(x, "approx_exp2_floor");
		float xf = 0.f;
		float y = c::approxExp2Floor<float>(x, &xf);
		return py::make_tuple(y, xf);
	}, py::arg("x"), "Deprecated in Rack: exp2_floor (T = float).");
	m.def("approx_exp2_floor", [](const float_4& x) {
		deprecatedAlias("approx_exp2_floor", "exp2_floor");
		float_4 xf = 0.f;
		float_4 y = c::approxExp2Floor<float_4>(x, &xf);
		return py::make_tuple(y, xf);
	}, py::arg("x"), "Deprecated in Rack: exp2_floor (T = float_4).");
	m.def("exp2_taylor5", [](float x) {
		exp2Domain(x, "exp2_taylor5");
		return c::exp2Taylor5<float>(x);
	}, py::arg("x"), "2**x with at most 6e-6 relative error (T = float).");
	m.def("exp2_taylor5", [](const float_4& x) { return c::exp2Taylor5<float_4>(x); }, py::arg("x"),
		"2**x with at most 6e-6 relative error (T = float_4).");
	m.def("approx_exp2_taylor5", [](float x) {
		deprecatedAlias("approx_exp2_taylor5", "exp2_taylor5");
		exp2Domain(x, "approx_exp2_taylor5");
		return c::approxExp2Taylor5<float>(x);
	}, py::arg("x"), "Deprecated in Rack: exp2_taylor5 (T = float).");
	m.def("approx_exp2_taylor5", [](const float_4& x) {
		deprecatedAlias("approx_exp2_taylor5", "exp2_taylor5");
		return c::approxExp2Taylor5<float_4>(x);
	}, py::arg("x"), "Deprecated in Rack: exp2_taylor5 (T = float_4).");

	// --- convert.hpp --------------------------------------------------------------
	py::class_<d::Int24>(m, "Int24", "rack::dsp::Int24: a packed 3-byte signed integer. A mutable value.")
		.def(py::init([] { return d::Int24(0); }), "0. (Rack's Int24() leaves it uninitialized.)")
		.def(py::init([](const py::int_& i) { return d::Int24(values::toInt<int32_t>(i, "Int24", "int32_t")); }), py::arg("i"),
			"Keeps the low 24 bits of an int32_t, sign-extended, as GCC stores Rack's bit-field.")
		.def_property("i", [](const d::Int24& v) { return (int32_t) v.i; },
			[](d::Int24& v, const py::int_& i) { v.i = values::toInt<int32_t>(i, "Int24.i", "int32_t"); },
			"The 24-bit field: -2**23 to 2**23 - 1. Assigning keeps the low 24 bits of an int32_t.")
		.def("__int__", [](d::Int24& v) { return (int32_t) v; }, "operator int32_t().")
		.def("__repr__", [](const d::Int24& v) { return "Int24(" + std::to_string((int32_t) v.i) + ")"; })
		.def("__copy__", [](const d::Int24& v) { return v; })
		.def("__deepcopy__", [](const d::Int24& v, py::dict) { return v; }, py::arg("memo"))
		.attr("__hash__") = py::none();
	py::implicitly_convertible<py::int_, d::Int24>();

	// The primary template is deleted; these are Rack's specializations, keyed by Python's names for the
	// C types: float (or ctypes.c_float), ctypes.c_int8, c_int16, c_int32, c_int64, and Int24.
	py::module_ ctypes = py::module_::import("ctypes");
	py::object f32 = py::module_::import("builtins").attr("float");
	py::object i8 = ctypes.attr("c_int8"), i16 = ctypes.attr("c_int16"), i32 = ctypes.attr("c_int32"), i64 = ctypes.attr("c_int64");
	py::object i24 = m.attr("Int24");
	py::dict conversions;
	auto names = [&](py::object t) {
		std::vector<py::object> out{t};
		if (t.is(f32))
			out.push_back(ctypes.attr("c_float"));
		return out;
	};
	auto add = [&](py::object to, py::object from, py::cpp_function fn) {
		for (const py::object& a : names(to))
			for (const py::object& b : names(from))
				conversions[py::make_tuple(a, b)] = fn;
	};
	add(f32, f32, convertFunction<float, float>());
	add(f32, i8, convertFunction<float, int8_t>());
	add(f32, i16, convertFunction<float, int16_t>());
	add(f32, i24, convertFunction<float, d::Int24>());
	add(f32, i32, convertFunction<float, int32_t>());
	add(f32, i64, convertFunction<float, int64_t>());
	add(i8, f32, convertFunction<int8_t, float>());
	add(i16, f32, convertFunction<int16_t, float>());
	add(i24, f32, convertFunction<d::Int24, float>());
	add(i32, f32, convertFunction<int32_t, float>());
	add(i64, f32, convertFunction<int64_t, float>());
	simd_bindings::addTemplate(m, "convert", conversions);

	// --- window.hpp ---------------------------------------------------------------
	RACKPY_T1("hann", hann, "p", "The Hann window at p in [0, 1]")
	RACKPY_T1("blackman_nuttall", blackmanNuttall, "p", "The Blackman-Nuttall window at p in [0, 1]")
	RACKPY_T1("blackman_harris", blackmanHarris, "p", "The Blackman-Harris window at p in [0, 1]")
#undef RACKPY_T1
	m.def("blackman", [](float alpha, float p) { return c::blackman<float>(alpha, p); }, py::arg("alpha"), py::arg("p"),
		"The Blackman window at p in [0, 1]; alpha is typically 0.16 (T = float).");
	m.def("blackman", [](const float_4& alpha, const float_4& p) { return c::blackman<float_4>(alpha, p); }, py::arg("alpha"),
		py::arg("p"), "The Blackman window at p in [0, 1]; alpha is typically 0.16 (T = float_4).");
	m.def("hann_window", [](const py::buffer& x, int len) { c::hannWindow(windowBuffer(x, len, "hann_window"), len); },
		py::arg("x"), py::arg("len"), "Multiplies the first len items of a float32 buffer by the Hann window, in place.");
	m.def("blackman_window", [](float alpha, const py::buffer& x, int len) {
		c::blackmanWindow(alpha, windowBuffer(x, len, "blackman_window"), len);
	}, py::arg("alpha"), py::arg("x"), py::arg("len"), "Multiplies the first len items by the Blackman window, in place.");
	m.def("blackman_nuttall_window", [](const py::buffer& x, int len) {
		c::blackmanNuttallWindow(windowBuffer(x, len, "blackman_nuttall_window"), len);
	}, py::arg("x"), py::arg("len"), "Multiplies the first len items by the Blackman-Nuttall window, in place.");
	m.def("blackman_harris_window", [](const py::buffer& x, int len) {
		c::blackmanHarrisWindow(windowBuffer(x, len, "blackman_harris_window"), len);
	}, py::arg("x"), py::arg("len"), "Multiplies the first len items by the Blackman-Harris window, in place.");

	// --- ode.hpp ------------------------------------------------------------------
#define RACKPY_ODE(pyname, method, doc) \
	m.def(pyname, [](float t, float dt, const py::buffer& x, int len, const py::function& f) { \
		stepFloats(method, t, dt, x, len, f, pyname); \
	}, py::arg("t"), py::arg("dt"), py::arg("x"), py::arg("len"), py::arg("f"), doc " (T = float): x is a writable float32 buffer."); \
	m.def(pyname, [](const float_4& t, const float_4& dt, const py::list& x, int len, const py::function& f) { \
		stepVectors(method, t, dt, x, len, f, pyname); \
	}, py::arg("t"), py::arg("dt"), py::arg("x"), py::arg("len"), py::arg("f"), doc " (T = float_4): x is a list of float_4.");

	RACKPY_ODE("step_euler", Method::Euler, "One Euler step of dx/dt = f(t, x), updating x in place")
	RACKPY_ODE("step_rk2", Method::RK2, "One 2nd-order Runge-Kutta step of dx/dt = f(t, x), updating x in place")
	RACKPY_ODE("step_rk4", Method::RK4, "One 4th-order Runge-Kutta step of dx/dt = f(t, x), updating x in place")
#undef RACKPY_ODE
}

}  // namespace dsp_bindings
}  // namespace rackpy
