// SPDX-License-Identifier: GPL-3.0-or-later
// Python buffers handed to Rack's code, which reads and writes them as plain contiguous memory.
#pragma once
#include <pybind11/pybind11.h>

#include <string>

namespace rackpy {
namespace runtime {

/** Fail unless a requested buffer's items lie one after another in C order. pybind11's buffer::request asks for
 strides, so a strided or reversed view (memoryview(b)[::-1], a NumPy slice) arrives with a pointer and a size
 that Rack's code would read or write past (RP-42). A dimension of length 0 or 1 has no meaningful stride. */
inline void requireContiguous(const pybind11::buffer_info& info, const char* what) {
	pybind11::ssize_t expected = info.itemsize;
	for (pybind11::ssize_t d = info.ndim - 1; d >= 0; d--) {
		if (info.shape[(size_t) d] > 1 && info.strides[(size_t) d] != expected)
			throw pybind11::value_error(std::string(what) + " needs a contiguous buffer (not a strided or reversed view)");
		expected *= info.shape[(size_t) d];
	}
}

/** A Frame's buffer as Rack's Frame<N> copies it: N contiguous float32 samples. A Python subclass can give any
 buffer (__buffer__), so the copy checks first (RP-42). */
inline void frameBuffer(const pybind11::buffer_info& info, pybind11::ssize_t n, const char* what) {
	if (info.itemsize != (pybind11::ssize_t) sizeof(float) || info.format != pybind11::format_descriptor<float>::format() ||
		info.size < n)
		throw pybind11::type_error(std::string(what) + " needs " + std::to_string(n) + " float32 samples");
	requireContiguous(info, what);
}

}  // namespace runtime
}  // namespace rackpy
