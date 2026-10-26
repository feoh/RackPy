// SPDX-License-Identifier: GPL-3.0-or-later
// NanoVG drawing (nanovg.cpp) and Rack's window resources (window/resources.cpp).
#pragma once
#include <pybind11/pybind11.h>

#include <functional>
#include <memory>
#include <string>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy { namespace drawing {
namespace py = pybind11;

/** An opaque handle to a native object Rack owns (an NVGcontext, the GLFWwindow, an
NSVGimage, the patch manager): identity and liveness only, never an address. */
struct NativeHandle {
	std::string kind;
	void* ptr = nullptr;
	/** False once Rack has replaced or deleted the object, or its context has closed. */
	std::function<bool()> valid;
	/** Keeps the owner alive where Python owns it (the Svg of an NSVGimage). */
	std::shared_ptr<void> keep;
	bool alive() const {
		runtime::requireMainThread("using a native Rack handle");
		return valid && valid();
	}
	void* get(const char* expected) const {
		if (kind != expected) throw py::type_error("expected a " + std::string(expected) + " handle, not " + kind);
		if (!alive()) throw runtime::StaleHandle(kind + " handle is stale: Rack replaced or deleted it");
		return ptr;
	}
};

/** Font handles Rack has given Python in this graphics context. NanoVG never deletes fonts and
numbers them from 0, so every handle up to the highest one seen exists. fontstash does not check
the handles its fallback functions take, so those take only known ones. */
void noteFont(int handle);
bool knownFont(int handle);

/** rackpy.raw.window.NativeHandle; bind_widgets() calls it before window.Svg. */
void bindNativeHandle(py::module_ window);
/** rackpy.raw.nanovg's drawing functions. */
void bindNanovg(py::module_ raw);
/** Window, Font, Image and the unit helpers in rackpy.raw.window. */
void bindWindowResources(py::module_ raw);
/** Context::window: the checked Window handle, or None (headless Rack). */
py::object windowObject(const std::shared_ptr<runtime::ContextToken>& token);
/** An opaque Context field (patch, midiLoopbackContext) as a NativeHandle, or None. */
py::object contextFieldHandle(const std::shared_ptr<runtime::ContextToken>& token, const char* field);

} }
