// SPDX-License-Identifier: GPL-3.0-or-later
// The laboratory's audio and MIDI drivers and devices (RP-22C, RP-22F): Python
// overrides of the driver and device virtuals, run where Rack's own port code
// calls them, on the main thread. They are never registered with Rack, so only
// ports Python made and connected reach them.
#pragma once
#include <pybind11/pybind11.h>

#include <optional>
#include <set>
#include <utility>

#include <rack.hpp>

#include "../bindings/support/overrides.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace laboratory {

namespace py = pybind11;

/** Call a laboratory driver or device override, or answer natively: as bindings::overrideOr, but `eligible`
(whether the call's ports may be handed to Python) runs only where Python may run, and `call` converts the
arguments and the result. With the GIL held (Python called Rack's code) an error reaches that caller; otherwise
callPython refuses Rack's threads and contains errors, and `base` answers. */
template <typename T, typename Self, typename Eligible, typename Base, typename Call>
T dispatch(const Self* self, const char* name, Eligible eligible, Base base, Call call) {
	if (!runtime::interpreterAlive())
		return base();
	std::optional<T> result;
	auto run = [&] {
		if (!eligible())
			return;
		if (auto f = bindings::pythonOverride(self, name))
			result.emplace(call(f));
	};
	if (PyGILState_Check())
		run();
	else
		runtime::callPython(name, run);
	return result ? std::move(*result) : base();
}

/** dispatch for a virtual without ports. */
template <typename T, typename Self, typename Base, typename Call>
T dispatch(const Self* self, const char* name, Base base, Call call) {
	return dispatch<T>(self, name, [] { return true; }, base, call);
}

/** A void virtual: `base` answers only if no override ran to completion. */
template <typename Self, typename Eligible, typename Base, typename Call>
void dispatchVoid(const Self* self, const char* name, Eligible eligible, Base base, Call call) {
	dispatch<bool>(self, name, eligible, [&] { base(); return true; }, [&](const bindings::PythonOverride& f) {
		call(f);
		return true;
	});
}

template <typename T>
std::set<T> valueSet(py::handle values) {
	std::set<T> out;
	for (py::handle value : py::reinterpret_borrow<py::iterable>(values))
		out.insert(value.cast<T>());
	return out;
}

/** The C++ object of a laboratory driver, or null while its __init__ has not made one (a GC pass can see it then). */
template <typename T>
T* constructed(PyObject* self) noexcept {
	try {
		return &py::cast<T&>(py::handle(self));
	}
	catch (...) {
		return nullptr;
	}
}

#ifdef RACKPY_TEST_HOOKS
/** The native objects behind the laboratory's Python drivers and devices, for test hooks that call them from
a Rack thread. */
rack::audio::Driver& audioDriverForTesting(py::handle driver);
rack::audio::Device& audioDeviceForTesting(py::handle device);
rack::midi::Driver& midiDriverForTesting(py::handle driver);
rack::midi::Device& midiDeviceForTesting(py::handle device);
#endif

}  // namespace laboratory
}  // namespace rackpy
