// SPDX-License-Identifier: GPL-3.0-or-later
// Family 1 (common values) bindings, RP-12. One function per header; common.cpp
// calls them all from bind_common(), which the generated registry calls.
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include <cstdint>
#include <limits>
#include <string>

namespace rackpy {
namespace values {

namespace py = pybind11;

void bindNanovgTypes(py::module_ raw);  // NVGcolor, the boundary type color.hpp uses
void bindMath(py::module_ raw);  // rack::math -> rackpy.raw.math
void bindColor(py::module_ raw);  // rack::color -> rackpy.raw.color
void bindCommon(py::module_ raw);  // rack:: from common.hpp -> rackpy.raw; literals -> rackpy.raw.literals
void bindQuantity(py::module_ raw);  // rack::Quantity -> rackpy.raw.Quantity; rackpy's ValueQuantity

/** Raise OverflowError naming the C++ type a Python int did not fit. */
[[noreturn]] void raiseOverflow(const char* what, const char* type);

/** A Python int as a C++ integer of type T, or OverflowError: never a silent wrap, and never a
quiet fall-through to a float overload. */
template <typename T>
T toInt(const py::int_& value, const char* what, const char* type) {
	int overflow = 0;
	long long v = PyLong_AsLongLongAndOverflow(value.ptr(), &overflow);
	if (v == -1 && PyErr_Occurred())
		throw py::error_already_set();
	if (overflow || v < (long long) std::numeric_limits<T>::min() || v > (long long) std::numeric_limits<T>::max())
		raiseOverflow(what, type);
	return (T) v;
}

/** The exact value of a C++ float, as Python's repr() of the same double shows it. */
std::string floatRepr(float value);

}  // namespace values
}  // namespace rackpy
