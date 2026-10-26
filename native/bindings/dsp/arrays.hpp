// SPDX-License-Identifier: GPL-3.0-or-later
// Live views of the fixed C++ array fields of Rack's DSP objects (RP-18C, RP-18D):
// IIRFilter::b, MidiParser::notes, MidiParser::pwFilters, ...
//
// A view keeps the object that owns the array alive and indexes it with checked
// indices (negative ones count from the end). Numbers read as Python values and
// are range-checked when written (OverflowError outside the C++ type); objects
// (float_4, ExponentialFilter, PulseGenerator) read as live views of the element.
// A read-only view (for Rack's const T* results, RP-19C) refuses writes and reads
// objects as copies.
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include <string>
#include <type_traits>
#include <utility>

#include "../values/values.hpp"

namespace rackpy {
namespace dsp_bindings {

namespace py = pybind11;

template <typename T>
struct ArrayView {
	T* data;
	size_t size;
	py::object owner;
	bool readonly = false;
	size_t index(int i) const {
		int n = (int) size;
		if (i < 0)
			i += n;
		if (i < 0 || i >= n)
			throw py::index_error("index out of range (the array has " + std::to_string(size) + " items)");
		return (size_t) i;
	}
};

template <typename T>
constexpr bool isNumber() {
	return std::is_arithmetic<T>::value;
}

template <typename T>
py::object item(py::object self, T* element) {
	if constexpr (std::is_same<T, bool>::value)
		return py::bool_(*element);
	else if constexpr (std::is_floating_point<T>::value)
		return py::float_(*element);
	else if constexpr (std::is_integral<T>::value)
		return py::int_((long long) *element);
	else
		return py::cast(element, py::return_value_policy::reference_internal, self);
}

template <typename T>
void assign(T* element, py::handle value, const char* name) {
	if constexpr (std::is_same<T, bool>::value) {
		if (!py::isinstance<py::bool_>(value))
			throw py::type_error(std::string(name) + " items are bools");
		*element = value.cast<bool>();
	}
	else if constexpr (std::is_integral<T>::value) {
		if (!py::isinstance<py::int_>(value))
			throw py::type_error(std::string(name) + " items are ints");
		*element = values::toInt<T>(value.cast<py::int_>(), name, "the array's C++ type");
	}
	else {
		*element = value.cast<T>();
	}
}

/** Binds ArrayView<T> as `name` in m (once per element type). */
template <typename T>
void bindArrayView(py::module_ m, const char* name) {
	py::class_<ArrayView<T>>(m, name, "A live view of a fixed array field of a Rack DSP object; it keeps the object alive.")
		.def("__len__", [](const ArrayView<T>& v) { return v.size; })
		.def("__getitem__", [](py::object self, int i) -> py::object {
			auto& v = self.cast<ArrayView<T>&>();
			if constexpr (!isNumber<T>())
				if (v.readonly)
					return py::cast(T(v.data[v.index(i)]));
			return item(self, &v.data[v.index(i)]);
		}, py::arg("i"))
		.def("__setitem__", [name](ArrayView<T>& v, int i, py::handle x) {
			if (v.readonly)
				throw py::type_error(std::string(name) + " view is read-only (Rack returns a const pointer)");
			assign(&v.data[v.index(i)], x, name);
		}, py::arg("i"), py::arg("x"))
		.def_property_readonly("readonly", [](const ArrayView<T>& v) { return v.readonly; })
		.def("tolist", [](py::object self) {
			auto& v = self.cast<ArrayView<T>&>();
			py::list out;
			for (size_t k = 0; k < v.size; k++) {
				if constexpr (isNumber<T>())
					out.append(item(self, &v.data[k]));
				else
					out.append(py::cast(T(v.data[k])));
			}
			return out;
		}, "The items as a list (copies of objects).")
		.def("__repr__", [name](py::object self) {
			return std::string(name) + "(" + py::repr(self.attr("tolist")()).cast<std::string>() + ")";
		});
}

template <typename T>
py::object view(py::object self, T* data, size_t n, bool readonly = false) {
	return py::cast(ArrayView<T>{data, n, std::move(self), readonly});
}

template <typename Class>
void valueCopies(Class& cls) {
	using F = typename Class::type;
	cls.def("__copy__", [](const F& f) { return f; }).def("__deepcopy__", [](const F& f, py::dict) { return f; }, py::arg("memo"));
	cls.attr("__hash__") = py::none();
}

}  // namespace dsp_bindings
}  // namespace rackpy
