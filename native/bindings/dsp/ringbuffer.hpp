// SPDX-License-Identifier: GPL-3.0-or-later
// dsp/ringbuffer.hpp -> rackpy.raw.dsp (RP-19C): the shared parts of the three
// ring buffer families (ringbuffer.cpp, ringbuffer_double.cpp, ringbuffer_apple.cpp).
//
// RackPy's generic contract for RingBuffer<T, S> and the others (coverage
// policy 10, an owner review item; docs/reference/dsp.md#ring-buffers):
// T is float, simd::float_4, or Frame<C> for C = 2, 8 and 16 (Rack's audio
// interfaces' frame sizes); S is a power of 2 from 16 to 32768, and for
// AppleRingBuffer from 16 to 4096 with N = 16 S. Elements cross as Python
// values (float, float_4, Frame[C]); bulk copies take float32 buffers holding
// n elements' lanes; Rack's pointers are live views of the inline data, which
// never moves, read-only for its const T* results.
#pragma once
#include "dsp.hpp"

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "../simd/simd.hpp"
#include "arrays.hpp"
#include "calls.hpp"

namespace rackpy {
namespace dsp_bindings {
namespace ring {

namespace d = rack::dsp;
using rack::simd::float_4;

/** How a ring buffer's element type crosses to Python. */
template <typename T>
struct Element;

template <>
struct Element<float> {
	static constexpr size_t lanes = 1;
	static std::string name() { return "float"; }
	static py::object key() { return py::module_::import("builtins").attr("float"); }
	static float from(py::handle h) { return h.cast<float>(); }
	static py::object to(const float& x) { return py::float_(x); }
};

template <>
struct Element<float_4> {
	static constexpr size_t lanes = 4;
	static std::string name() { return "float_4"; }
	static py::object key() { return py::type::of<float_4>(); }
	static float_4 from(py::handle h) { return h.cast<float_4>(); }
	static py::object to(const float_4& x) { return py::cast(x); }
};

template <size_t C>
struct Element<d::Frame<C>> {
	static constexpr size_t lanes = C;
	static std::string name() { return "Frame" + std::to_string(C); }
	/** rackpy.raw._dsp writes Frame in Python after this module loads, and re-keys these by Frame[C]. */
	static py::object key() { return py::make_tuple("Frame", C); }
	static py::object type() {
		return py::module_::import("rackpy.raw.dsp").attr("Frame")[py::make_tuple(C, py::module_::import("builtins").attr("float"))];
	}
	static d::Frame<C> from(py::handle h) {
		if (!py::isinstance(h, type()))
			throw py::type_error("the elements are Frame[" + std::to_string(C) + "] objects");
		py::buffer_info info = py::reinterpret_borrow<py::buffer>(h).request();
		d::Frame<C> frame;
		std::memcpy(frame.samples, info.ptr, sizeof(frame.samples));
		return frame;
	}
	static py::object to(const d::Frame<C>& x) {
		py::object out = type()();
		py::buffer_info info = py::reinterpret_borrow<py::buffer>(out).request(true);
		std::memcpy(info.ptr, x.samples, sizeof(x.samples));
		return out;
	}
};

/** A live view of n elements at data: of T for float and float_4, of n * C floats for Frame<C>. */
template <typename T>
py::object elements(py::object self, const T* data, size_t n, bool readonly) {
	static_assert(sizeof(T) == sizeof(float) * Element<T>::lanes, "elements are packed lanes");
	T* p = const_cast<T*>(data);  // a read-only view never writes through it
	if constexpr (std::is_same<T, float>::value || std::is_same<T, float_4>::value)
		return view(std::move(self), p, n, readonly);
	else
		return view(std::move(self), reinterpret_cast<float*>(p), n * Element<T>::lanes, readonly);
}

/** A contiguous float32 buffer holding n elements' lanes. */
template <typename T>
float* lanes(const py::buffer& buf, bool writable, size_t n, const char* what) {
	py::buffer_info info = buf.request(writable);
	if (!info.item_type_is_equivalent_to<float>())
		throw py::type_error(std::string(what) + " needs a buffer of float32 ('f') items, not '" + info.format + "'");
	if (info.ndim != 1 || info.strides[0] != (py::ssize_t) sizeof(float))
		throw py::value_error(std::string(what) + " needs a contiguous one-dimensional buffer");
	size_t need = n * Element<T>::lanes;
	if ((size_t) info.size < need)
		throw py::value_error(std::string(what) + " needs at least " + std::to_string(need) + " floats (" + std::to_string(n) +
			" elements of " + std::to_string(Element<T>::lanes) + "); the buffer has " + std::to_string(info.size));
	return static_cast<float*>(info.ptr);
}

/** Aligned scratch for n elements (at least one, so Rack's memcpy never gets a null pointer). */
template <typename T>
std::vector<T> scratch(size_t n) {
	return std::vector<T>(n ? n : 1);
}

template <typename R>
std::string className(const char* family, const std::string& element, const std::vector<size_t>& sizes) {
	std::string name = std::string(family) + "_" + element;
	for (size_t s : sizes)
		name += "_" + std::to_string(s);
	return name;
}

void bindDouble(py::module_ scope, py::dict& specs);
void bindApple(py::module_ scope, py::dict& specs);

}  // namespace ring
}  // namespace dsp_bindings
}  // namespace rackpy
