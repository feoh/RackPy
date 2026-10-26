// SPDX-License-Identifier: GPL-3.0-or-later
// Family 5 adapter: the generated registry calls bind_window(). So far it binds
// the Context accessor (RP-26B): rack::contextGet, the APP macro and
// Context::event. rackpy.raw.window.Svg is bound by bind_widgets(), which needs
// it first (family 4 comes first); the rest of rack::window is RP-27.
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"
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
	py::class_<ContextHandle>(raw, "Context", py::is_final(),
		"Rack's Context (APP): the objects of one Rack instance. Partial: the event state so far; the engine, "
		"scene, window, history and patch come with their families.")
		.def_property_readonly("alive", [](const ContextHandle& h) { return h.token->alive(); })
		.def_property_readonly("event", [](const ContextHandle& h) {
			runtime::requireMainThread("using Rack's event state");
			h.get();
			return widgets::eventStateObject(h.token);
		}, "Rack's input dispatcher, a rackpy.raw.widget.EventState.");
	raw.def("context_get", &currentContext,
		"Rack's contextGet(): the Rack context current on this thread, or None (a thread Rack did not start, or "
		"before Rack's UI exists).");
	raw.def("APP", &currentContext, "The APP macro: contextGet().");
}

}  // namespace bindings
}  // namespace rackpy
