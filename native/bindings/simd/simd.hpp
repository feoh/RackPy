// SPDX-License-Identifier: GPL-3.0-or-later
// Family 6 SIMD bindings (RP-20): rack::simd -> rackpy.raw.simd, and the
// global sse_mathfun functions -> rackpy.raw.sse_mathfun. dsp.cpp calls them
// from bind_dsp(), which the generated registry calls.
//
// Python sees Rack's two vector types, float_4 and int32_4, as value classes
// holding the C++ vector. __m128 and __m128i, the compiler's register types
// in Rack's signatures, are float_4 and int32_4 at the Python boundary, and
// std::complex<float_4> is a (real, imag) tuple of float_4.
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include <complex>

namespace rackpy {
namespace simd_bindings {

namespace py = pybind11;

void bindVectors(py::module_ simd);  // float_4, int32_4, their operators, and the Vector template
void bindFunctions(py::module_ simd);  // functions.hpp, with the std:: and math:: scalar functions it imports
void bindSseMathfun(py::module_ raw);  // sse_mathfun.h and sse_mathfun_extension.h -> rackpy.raw.sse_mathfun

/** Adds m.<name>, a C++ function template that Python specializes by subscripting
(`movemask_inverse[float_4]`): specializations maps each (template arguments...) tuple to a function. */
void addTemplate(py::module_ m, const char* name, py::dict specializations);

}  // namespace simd_bindings
}  // namespace rackpy

namespace pybind11 {
namespace detail {

/** std::complex<float_4> <-> (real, imag), a tuple of two float_4 (functions.hpp's abs and arg;
dsp::IIRFilter's transfer function for float_4). */
template <>
struct type_caster<std::complex<rack::simd::float_4>> {
	using V = rack::simd::float_4;
	PYBIND11_TYPE_CASTER(std::complex<V>, const_name("tuple[float_4, float_4]"));

	bool load(handle src, bool convert) {
		if (!isinstance<tuple>(src))
			return false;
		auto t = reinterpret_borrow<tuple>(src);
		if (t.size() != 2)
			return false;
		make_caster<V> re, im;
		if (!re.load(t[0], convert) || !im.load(t[1], convert))
			return false;
		value = std::complex<V>(cast_op<V>(re), cast_op<V>(im));
		return true;
	}

	static handle cast(const std::complex<V>& src, return_value_policy, handle) {
		return make_tuple(src.real(), src.imag()).release();
	}
};

}  // namespace detail
}  // namespace pybind11
