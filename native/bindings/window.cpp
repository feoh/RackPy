// SPDX-License-Identifier: GPL-3.0-or-later
// Family 5 adapter: the generated registry calls bind_window(). It binds the
// Context accessor (RP-26B: contextGet, APP, contextSet, appGet, Context::event)
// and, from RP-27A, Context's window and opaque fields, rack::window's resources
// (drawing/resources.cpp) and NanoVG drawing (drawing/nanovg.cpp).
// rackpy.raw.window.Svg is bound by bind_widgets(), which needs it first.
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"
#include "drawing/drawing.hpp"
#include "ui/ui.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;

namespace {

/** Rack's Context, checked: the context RackPy is bound to, on the thread it is current on. */
struct ContextHandle {
	std::shared_ptr<runtime::ContextToken> token;
	rack::Context& get() const {
		if (!token->alive())
			throw runtime::StaleHandle("Rack's context has closed");
		runtime::checkContext(*token);
		return *token->context;
	}
};

py::object currentContext() {
	auto token = runtime::binding().current();
	if (!token || !token->alive() || rack::contextGet() != token->context)
		return py::none();
	return runtime::identity(token->context, token->generation, [&] { return py::cast(ContextHandle{token}); });
}

}  // namespace

void bind_window(py::module_ raw) {
	py::class_<ContextHandle> context(raw, "Context", py::is_final(),
		"Rack's Context (APP): the objects of one Rack instance. Partial: the engine, scene and history come with "
		"their families.");
	context
		.def_property_readonly("alive", [](const ContextHandle& h) { return h.token->alive(); })
		.def_property_readonly("event", [](const ContextHandle& h) {
			runtime::requireMainThread("using Rack's event state");
			h.get();
			return widgets::eventStateObject(h.token);
		}, "Rack's input dispatcher, a rackpy.raw.widget.EventState.");
	context.def_property_readonly("window", [](const ContextHandle& h) {
			runtime::requireMainThread("using Rack's window");
			h.get();
			return drawing::windowObject(h.token);
		}, "Rack's rackpy.raw.window.Window, or None when Rack is headless.")
		.def_property_readonly("patch", [](const ContextHandle& h) {
			runtime::requireMainThread("reading Rack's context");
			h.get();
			return drawing::contextFieldHandle(h.token, "patch");
		}, "Rack's patch manager, as an opaque NativeHandle (patch.hpp is outside rack.hpp).")
		.def_property_readonly("midi_loopback_context", [](const ContextHandle& h) {
			runtime::requireMainThread("reading Rack's context");
			h.get();
			return drawing::contextFieldHandle(h.token, "midiLoopbackContext");
		}, "Rack's MIDI loopback context, as an opaque NativeHandle (midiloopback.hpp is outside rack.hpp).");
	raw.def("context_set", [](py::object value) {
		std::shared_ptr<runtime::ContextToken> token;
		if (!value.is_none()) {
			token = value.cast<const ContextHandle&>().token;
			if (!token->alive()) throw runtime::StaleHandle("Rack's context has closed");
		}
		if (runtime::isMainThread()) {
			// Rack and RackPy rely on the main thread's context; only setting the same one is allowed.
			if (!token || rack::contextGet() != token->context)
				throw runtime::WrongContext("the main thread's Rack context belongs to Rack and cannot be changed");
			return;
		}
		rack::contextSet(token ? token->context : nullptr);
	}, py::arg("context"),
		"Rack's contextSet(): makes the Context (or None) current on this thread, as a plugin thread does before using "
		"APP. On Rack's main thread only the current context is accepted.");
	raw.def("app_get", [] {
		if (PyErr_WarnEx(PyExc_DeprecationWarning, "app_get mirrors deprecated appGet; use context_get", 1) < 0)
			throw py::error_already_set();
		return currentContext();
	}, "Rack's deprecated appGet(): contextGet().");
	raw.def("context_get", &currentContext,
		"Rack's contextGet(): the Rack context current on this thread, or None (a thread Rack did not start, or "
		"before Rack's UI exists).");
	raw.def("APP", &currentContext, "The APP macro: contextGet().");
	drawing::bindWindowResources(raw);
	drawing::bindNanovg(raw);
	// Rack's deprecated window::SVG typedef: the same class, with a warning when used.
	auto window = raw.attr("window").cast<py::module_>();
	py::list names;
	for (auto name : window.attr("__dict__").cast<py::dict>())
		if (name.first.cast<std::string>().rfind("_", 0) != 0) names.append(name.first);
	names.append("SVG");
	window.attr("__all__") = names;
	window.attr("__getattr__") = py::cpp_function([](const std::string& name) -> py::object {
		if (name != "SVG") throw py::attribute_error("module 'rackpy.raw.window' has no attribute '" + name + "'");
		if (PyErr_WarnEx(PyExc_DeprecationWarning, "window.SVG mirrors deprecated window::SVG; use window.Svg", 1) < 0)
			throw py::error_already_set();
		return py::module_::import("_rackpy_host").attr("raw").attr("window").attr("Svg");
	});
}

}  // namespace bindings
}  // namespace rackpy
