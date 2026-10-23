// SPDX-License-Identifier: GPL-3.0-or-later
// Python that runs inside Rack's own C++ code (RP-15A): a data handler or a
// JSON override that Rack's serializers call. Exceptions never cross Rack's
// frames. When Python called the Rack code (an ErrorCapture is active on this
// thread, with the GIL held), the first error is kept for that caller to
// re-raise; when Rack called, callPython contains and records it.
#pragma once
#include <pybind11/pybind11.h>

#include <exception>
#include <utility>

#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

struct ErrorCapture {
	ErrorCapture() : previous(active()) { active() = &error; }
	~ErrorCapture() { active() = previous; }
	ErrorCapture(const ErrorCapture&) = delete;
	ErrorCapture& operator=(const ErrorCapture&) = delete;
	/** Re-raise the first error the Python code inside raised, if any. */
	void rethrow() {
		if (error)
			std::rethrow_exception(std::exchange(error, nullptr));
	}
	static std::exception_ptr*& active() {
		static thread_local std::exception_ptr* slot = nullptr;
		return slot;
	}

private:
	std::exception_ptr error;
	std::exception_ptr* previous;
};

/** Run `f`, which calls Python, from inside Rack's code. False if it failed or did not run. */
template <typename F>
bool runPythonHook(const char* what, F&& f) noexcept {
	if (!runtime::interpreterAlive())
		return false;
	std::exception_ptr* capture = ErrorCapture::active();
	if (capture && PyGILState_Check()) {
		try {
			f();
			return true;
		}
		catch (...) {
			if (!*capture)
				*capture = std::current_exception();
			return false;
		}
	}
	return runtime::callPython(what, f);
}

}  // namespace bindings
}  // namespace rackpy
