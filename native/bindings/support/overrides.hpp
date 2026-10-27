// SPDX-License-Identifier: GPL-3.0-or-later
// Calling Python overrides of Rack virtuals from trampolines (ADR 009, decision 6).
//
// From Python (GIL held), an override runs normally and its errors propagate
// to the Python caller. From Rack (no GIL), it runs through callPython, which
// refuses Rack's threads and catches everything; if it fails or there is no
// override, the C++ base implementation answers, so Rack always gets a value.
#pragma once
#include <pybind11/pybind11.h>

#include <optional>
#include <utility>

#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;

/** Bounds C++ frames hidden from Python's recursion counter. Needs the GIL.
64 live override calls leave ample stack for Rack's intervening native frames;
the counter is shared by all trampoline families and resets on every unwind. */
struct OverrideDepth {
	static inline thread_local unsigned depth = 0;
	// Numeric casters may clear an error and retry Python conversion. Refuse
	// those retries until the whole native callback chain has unwound.
	static inline thread_local bool exhausted = false;
	OverrideDepth() {
		if (exhausted || depth >= 64) {
			exhausted = true;
			PyErr_SetString(PyExc_RecursionError, "RackPy Python overrides nested more than 64 deep");
			throw py::error_already_set();
		}
		++depth;
	}
	~OverrideDepth() { if (--depth == 0) exhausted = false; }
	OverrideDepth(const OverrideDepth&) = delete;
	OverrideDepth& operator=(const OverrideDepth&) = delete;
};

/** A resolved override whose entire lifetime (including argument/result conversion
and bound-method destruction) is bounded. Never copy, move or slice to py::function. */
class PythonOverride {
	// Destroy the function first: dropping its last owner may itself reenter Python.
	std::optional<OverrideDepth> depth;
	py::function function;
public:
	PythonOverride() = default;
	explicit PythonOverride(py::function f) : depth(std::in_place), function(std::move(f)) {}
	PythonOverride(const PythonOverride&) = delete;
	PythonOverride& operator=(const PythonOverride&) = delete;
	PythonOverride(PythonOverride&&) = delete;
	PythonOverride& operator=(PythonOverride&&) = delete;
	template<class... Args> py::object operator()(Args&&... args) const {
		return function(std::forward<Args>(args)...);
	}
	explicit operator bool() const { return bool(function); }
};

/** Resolve using pybind11 3.1's registered-instance lookup and cpp_function test,
but NOT its Python-frame test: real native reentry must dispatch again. Base
bindings explicitly call qualified C++ implementations for RackPy trampolines,
so super() does not need a frame-name heuristic. No negative cache: late class
and instance monkeypatches must remain visible. Needs the GIL. `self` is the
most-derived bound C++ type (not the trampoline type). */
template<class Self>
PythonOverride pythonOverride(const Self* self, const char* name) {
	py::function f;
	{
		// Attribute descriptors / __getattribute__ can themselves reenter native code.
		OverrideDepth lookup;
		auto* info = py::detail::get_type_info(typeid(Self));
		py::handle object = info ? py::detail::get_object_handle(self, info) : py::handle();
		if (!object) return {};
		f = py::getattr(object, name, py::function());
		if (!f || f.is_cpp_function()) return {};
	}
	// C++17 guaranteed prvalue elision: the nonmovable guard lives at the call site.
	return PythonOverride(std::move(f));
}

/** `self` must be the most-derived *bound* C++ type: pybind11 finds the Python object by exact type. */
template <typename T, typename Self, typename Base, typename... Args>
T overrideOr(const Self* self, const char* name, Base base, Args&&... args) {
	// PyGILState_Check() returns 1 once finalization has started, so test the interpreter first.
	if (!runtime::interpreterAlive())
		return base();
	if (PyGILState_Check()) {
		if (auto f = pythonOverride(self, name))
			return f(std::forward<Args>(args)...).template cast<T>();
		return base();
	}
	bool answered = false;
	T result{};
	runtime::callPython(name, [&] {
		if (auto f = pythonOverride(self, name)) {
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
		if (auto f = pythonOverride(self, name)) {
			f(std::forward<Args>(args)...);
			return;
		}
		base();
		return;
	}
	bool answered = false;
	runtime::callPython(name, [&] {
		if (auto f = pythonOverride(self, name)) {
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
