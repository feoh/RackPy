// SPDX-License-Identifier: GPL-3.0-or-later
// rack::widget::EventState (RP-26B): Rack's input dispatcher, reached through
// rackpy.raw.context_get().event. A checked handle bound to Rack's context:
// main thread only, stale once the context closes. Widget pointers are read
// through RackPy's widget identities and written only with live widgets;
// Rack's finalizeWidget clears them when a widget leaves its tree or is deleted.
#include "../widget/variants.hpp"
#include "ui.hpp"

#include <pybind11/stl.h>

namespace rackpy {
namespace widgets {

namespace {

using ES = rack::widget::EventState;

struct EventStateHandle {
	std::shared_ptr<runtime::ContextToken> token;
	ES& get() const {
		runtime::requireMainThread("using Rack's event state");
		if (!token->alive())
			throw runtime::StaleHandle("Rack's context, and its event state, has closed");
		runtime::checkContext(*token);
		if (!token->context->event)
			throw runtime::StaleHandle("Rack's context has no event state");
		return *token->context->event;
	}
};

template <W* ES::*Field>
void widgetField(py::class_<EventStateHandle>& cls, const char* name, const char* doc) {
	cls.def_property(name, [](const EventStateHandle& h) { return wrapper(h.get().*Field); },
		[](const EventStateHandle& h, py::object value) {
			ES& e = h.get();
			e.*Field = argument(value);  // None, or a live widget
		}, doc);
}

/** Dispatching runs Rack's event handlers, including Python overrides: not
from inside another widget callback, where the tree may be mid-iteration. */
ES& dispatching(const EventStateHandle& h, const char* what) {
	ES& e = h.get();
	checkMutation();
	(void) what;
	return e;
}

}  // namespace

py::object eventStateObject(const std::shared_ptr<runtime::ContextToken>& token) {
	if (!token || !token->context->event)
		return py::none();
	return runtime::identity(token->context->event, token->generation, [&] { return py::cast(EventStateHandle{token}); });
}

void bindEventState(py::module_ widget) {
	auto cls = py::class_<EventStateHandle>(widget, "EventState", py::is_final(),
		"Rack's input dispatcher (rack::widget::EventState), from rackpy.raw.context_get().event. Main thread; "
		"stale once Rack's context closes.");
	cls.def_property_readonly("root_widget", [](const EventStateHandle& h) { return wrapper(h.get().rootWidget); },
		"Rack's scene. Read-only: Rack dispatches every event from it.");
	widgetField<&ES::hoveredWidget>(cls, "hovered_widget",
		"The raw field: writing it skips Rack's Enter/Leave events (set_hovered_widget sends them).");
	widgetField<&ES::draggedWidget>(cls, "dragged_widget", "The raw field (set_dragged_widget sends drag events).");
	widgetField<&ES::dragHoveredWidget>(cls, "drag_hovered_widget", "The raw field (set_drag_hovered_widget sends events).");
	widgetField<&ES::selectedWidget>(cls, "selected_widget", "The raw field (set_selected_widget sends Select/Deselect).");
	widgetField<&ES::lastClickedWidget>(cls, "last_clicked_widget", "The raw field, for double-click detection.");
	cls.def_property("drag_button", [](const EventStateHandle& h) { return h.get().dragButton; },
			[](const EventStateHandle& h, int v) { h.get().dragButton = v; })
		.def_property("last_click_time", [](const EventStateHandle& h) { return h.get().lastClickTime; },
			[](const EventStateHandle& h, double v) { h.get().lastClickTime = v; })
		.def_property("held_keys", [](const EventStateHandle& h) { return h.get().heldKeys; },
			[](const EventStateHandle& h, std::set<int> keys) { h.get().heldKeys = std::move(keys); },
			"A copy of the GLFW keys Rack considers held; assign a set to change it.")
		.def("get_root_widget", [](const EventStateHandle& h) { return wrapper(h.get().getRootWidget()); })
		.def("get_hovered_widget", [](const EventStateHandle& h) { return wrapper(h.get().getHoveredWidget()); })
		.def("get_dragged_widget", [](const EventStateHandle& h) { return wrapper(h.get().getDraggedWidget()); })
		.def("get_drag_hovered_widget", [](const EventStateHandle& h) { return wrapper(h.get().getDragHoveredWidget()); })
		.def("get_selected_widget", [](const EventStateHandle& h) { return wrapper(h.get().getSelectedWidget()); });

	auto setter = [&cls](const char* name, void (ES::*method)(W*), const char* deprecatedName) {
		cls.def(name, [method](const EventStateHandle& h, py::object w) {
			ES& e = dispatching(h, "setting widget focus");
			(e.*method)(argument(w));
		}, py::arg("w"));
		cls.def(deprecatedName, [method, name](const EventStateHandle& h, py::object w) {
			if (PyErr_WarnEx(PyExc_DeprecationWarning, (std::string("EventState.") + name + " is Rack's current name").c_str(), 1) < 0)
				throw py::error_already_set();
			ES& e = dispatching(h, "setting widget focus");
			(e.*method)(argument(w));
		}, py::arg("w"), "Rack's deprecated spelling.");
	};
	setter("set_hovered_widget", &ES::setHoveredWidget, "set_hovered");
	setter("set_drag_hovered_widget", &ES::setDragHoveredWidget, "set_drag_hovered");
	setter("set_selected_widget", &ES::setSelectedWidget, "set_selected");
	cls.def("set_dragged_widget", [](const EventStateHandle& h, py::object w, int button) {
			dispatching(h, "dragging").setDraggedWidget(argument(w), button);
		}, py::arg("w"), py::arg("button"))
		.def("set_dragged", [](const EventStateHandle& h, py::object w, int button) {
			if (PyErr_WarnEx(PyExc_DeprecationWarning, "EventState.set_dragged_widget is Rack's current name", 1) < 0)
				throw py::error_already_set();
			dispatching(h, "dragging").setDraggedWidget(argument(w), button);
		}, py::arg("w"), py::arg("button"), "Rack's deprecated spelling.")
		.def("finalize_widget", [](const EventStateHandle& h, py::object w) { h.get().finalizeWidget(argument(w)); },
			py::arg("w"), "Forget `w` wherever Rack's event state refers to it, with Rack's Leave/Deselect events.");

	// The dispatchers: what GLFW's callbacks do. They run widgets' handlers, Python ones included.
	cls.def("handle_button", [](const EventStateHandle& h, rack::math::Vec pos, int button, int action, int mods) {
			return dispatching(h, "dispatching a button").handleButton(pos, button, action, mods);
		}, py::arg("pos"), py::arg("button"), py::arg("action"), py::arg("mods"))
		.def("handle_hover", [](const EventStateHandle& h, rack::math::Vec pos, rack::math::Vec delta) {
			return dispatching(h, "dispatching a hover").handleHover(pos, delta);
		}, py::arg("pos"), py::arg("mouse_delta"))
		.def("handle_leave", [](const EventStateHandle& h) { return dispatching(h, "dispatching a leave").handleLeave(); })
		.def("handle_scroll", [](const EventStateHandle& h, rack::math::Vec pos, rack::math::Vec delta) {
			return dispatching(h, "dispatching a scroll").handleScroll(pos, delta);
		}, py::arg("pos"), py::arg("scroll_delta"))
		.def("handle_text", [](const EventStateHandle& h, rack::math::Vec pos, uint32_t codepoint) {
			if (codepoint > 0x10FFFF)
				throw py::value_error("codepoint must be a Unicode code point");
			return dispatching(h, "dispatching text").handleText(pos, codepoint);
		}, py::arg("pos"), py::arg("codepoint"))
		.def("handle_key", [](const EventStateHandle& h, rack::math::Vec pos, int key, int scancode, int action, int mods) {
			return dispatching(h, "dispatching a key").handleKey(pos, key, scancode, action, mods);
		}, py::arg("pos"), py::arg("key"), py::arg("scancode"), py::arg("action"), py::arg("mods"))
		.def("handle_drop", [](const EventStateHandle& h, rack::math::Vec pos, std::vector<std::string> paths) {
			return dispatching(h, "dispatching a drop").handleDrop(pos, paths);
		}, py::arg("pos"), py::arg("paths"))
		.def("handle_dirty", [](const EventStateHandle& h) { return dispatching(h, "dispatching dirty").handleDirty(); });
}

}  // namespace widgets
}  // namespace rackpy
