// SPDX-License-Identifier: GPL-3.0-or-later
// Calling Python overrides of Rack virtuals from trampolines (ADR 009, decision 6).
//
// From Python (GIL held), an override runs normally and its errors propagate
// to the Python caller. From Rack (no GIL), it runs through callPython, which
// refuses Rack's threads and catches everything; if it fails or there is no
// override, the C++ base implementation answers, so Rack always gets a value.
#pragma once
#include <pybind11/pybind11.h>

#include <utility>

#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;

/** `self` must be the most-derived *bound* C++ type: pybind11 finds the Python object by exact type. */
template <typename T, typename Self, typename Base, typename... Args>
T overrideOr(const Self* self, const char* name, Base base, Args&&... args) {
	// PyGILState_Check() returns 1 once finalization has started, so test the interpreter first.
	if (!runtime::interpreterAlive())
		return base();
	if (PyGILState_Check()) {
		if (py::function f = py::get_override(self, name))
			return f(std::forward<Args>(args)...).template cast<T>();
		return base();
	}
	bool answered = false;
	T result{};
	runtime::callPython(name, [&] {
		if (py::function f = py::get_override(self, name)) {
			result = f(std::forward<Args>(args)...).template cast<T>();
			answered = true;
		}
	});
	return answered ? result : base();
}

template <typename Self, typename Base, typename... Args>
void overrideOrVoid(const Self* self, const char* name, Base base, Args&&... args) {
	if (!runtime::interpreterAlive()) {
		base();
		return;
	}
	if (PyGILState_Check()) {
		if (py::function f = py::get_override(self, name)) {
			f(std::forward<Args>(args)...);
			return;
		}
		base();
		return;
	}
	bool answered = false;
	runtime::callPython(name, [&] {
		if (py::function f = py::get_override(self, name)) {
			f(std::forward<Args>(args)...);
			answered = true;
		}
	});
	if (!answered)
		base();
}

/** Call a Rack API that is deprecated on purpose: deprecated declarations stay mapped (and warn in Python). */
#define RACKPY_CALL_DEPRECATED(...) \
	_Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") __VA_ARGS__ \
		_Pragma("GCC diagnostic pop")

/** Emit a DeprecationWarning for a deprecated Rack API (docs/coverage.md, policy 5). Needs the GIL. */
inline void deprecated(const char* message) {
	if (PyErr_WarnEx(PyExc_DeprecationWarning, message, 1) < 0)
		throw py::error_already_set();
}

}  // namespace bindings
}  // namespace rackpy
