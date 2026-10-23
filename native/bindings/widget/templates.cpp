// SPDX-License-Identifier: GPL-3.0-or-later
// Widget's member templates and the widget event helper functions. Templates
// over widget types take a class, including Python subclasses (docs/coverage.md,
// policy 10); the recursion templates take the handler method that the C++
// member pointer would name.
#include "behavior.hpp"
#include "rackpy/runtime/threads.hpp"

#include <unordered_map>

namespace rackpy { namespace widgets {
namespace {

using Match = std::function<bool(W*)>;

template<class T> Match castTo() { return [](W* w) { return dynamic_cast<T*>(w) != nullptr; }; }

/** dynamic_cast<T*> for the bound SDK classes; isinstance for Python subclasses of them. */
Match matcher(py::module_ mod, py::handle cls) {
	py::handle widget = mod.attr("Widget");
	if (!PyType_Check(cls.ptr()) || !PyObject_IsSubclass(cls.ptr(), widget.ptr()))
		throw py::type_error("expected rackpy.raw.widget.Widget or a subclass of it");
	const std::pair<const char*, Match> native[] = {
		{"Widget", castTo<W>()},
		{"TransparentWidget", castTo<rack::widget::TransparentWidget>()},
		{"OpaqueWidget", castTo<rack::widget::OpaqueWidget>()},
		{"ZoomWidget", castTo<rack::widget::ZoomWidget>()},
		{"TransformWidget", castTo<rack::widget::TransformWidget>()},
		{"SvgWidget", castTo<rack::widget::SvgWidget>()},
		{"FramebufferWidget", castTo<rack::widget::FramebufferWidget>()},
		{"OpenGlWidget", castTo<rack::widget::OpenGlWidget>()},
	};
	for (const auto& [name, match] : native)
		if (cls.is(mod.attr(name)))
			return match;
	// A Python class can only describe widgets that have its Python peer.
	auto type = py::reinterpret_borrow<py::object>(cls);
	return [type](W* w) {
		if (!managedWidget(w))
			return false;
		return py::isinstance(wrapper(w), type);
	};
}

W* ancestorOf(W& w, const Match& match) {
	for (W* p = w.parent; p; p = p->parent)
		if (match(p))
			return p;
	return nullptr;
}

W* descendantOf(W& w, const Match& match) {
	for (W* child : w.children) {
		if (match(child))
			return child;
		if (W* found = descendantOf(*child, match))
			return found;
	}
	return nullptr;
}

/** Rack's recurseEvent and recursePositionEvent, for one event type. */
template<class E> void recurse(W& w, void (W::*method)(const E&), const E& e, bool position) {
	for (auto it = w.children.rbegin(); it != w.children.rend(); it++) {
		if (!e.isPropagating())
			break;
		W* child = *it;
		E copy = e;
		if constexpr (std::is_base_of<W::PositionBaseEvent, E>::value) {
			if (position) {
				if (!child->visible || !child->box.contains(e.pos))
					continue;
				copy.pos = e.pos.minus(child->box.pos);
			}
		}
		(child->*method)(copy);
	}
}

using Recurse = std::function<void(W&, const EventRef&, bool)>;

const std::unordered_map<std::string, Recurse>& recursions() {
	static const std::unordered_map<std::string, Recurse> table = {
#define RECURSE(E, native, python) \
		{#python, [](W& w, const EventRef& ref, bool position) { \
			if (position && !std::is_base_of<W::PositionBaseEvent, W::E>::value) \
				throw py::type_error(#E " has no position; use recurse_event"); \
			recurse<W::E>(w, &W::native, nativeEvent<W::E>(ref, #E), position); \
		}},
		RACKPY_WIDGET_EVENTS(RECURSE)
#undef RECURSE
	};
	return table;
}

void recurseFrom(py::object self, py::handle method, py::object event, bool position) {
	auto owner = state(self);
	checkTraversal(owner);
	auto& w = checked(owner);
	// A C++ member pointer to a virtual handler dispatches virtually: match it by name.
	auto name = py::getattr(method, "__name__", py::none());
	auto it = name.is_none() ? recursions().end() : recursions().find(name.cast<std::string>());
	if (it == recursions().end())
		throw py::type_error("expected a Widget event handler such as Widget.on_hover");
	auto ref = eventData(event);
	CallbackGuard guard;  // children's handlers may not change this tree while it is iterated
	it->second(w, ref, position);
	if (ref->base)
		captureEventTarget(ref->base().context);
}

}  // namespace

void bindTemplates(py::module_ mod) {
	auto cls = py::reinterpret_borrow<py::class_<W>>(mod.attr("Widget"));
	cls.def("get_ancestor_of_type", [mod](py::object self, py::object type) {
		auto& w = checked(state(self));
		auto match = matcher(mod, type);
		CallbackGuard guard;  // isinstance can run Python: keep the tree still
		return wrapper(ancestorOf(w, match));
	}, py::arg("cls"), "Rack's getAncestorOfType<T>(): the nearest ancestor that is a cls, or None.")
		.def("get_first_descendant_of_type", [mod](py::object self, py::object type) {
			auto owner = state(self);
			checkTraversal(owner);
			auto& w = checked(owner);
			auto match = matcher(mod, type);
			CallbackGuard guard;
			return wrapper(descendantOf(w, match));
		}, py::arg("cls"), "Rack's getFirstDescendantOfType<T>(): depth-first, in child order.")
		.def("recurse_event", [](py::object self, py::object method, py::object event) {
			recurseFrom(self, method, event, false);
		}, py::arg("method"), py::arg("event"),
			"Rack's recurseEvent: call method on each child, topmost first, while the event propagates.")
		.def("recurse_position_event", [](py::object self, py::object method, py::object event) {
			recurseFrom(self, method, event, true);
		}, py::arg("method"), py::arg("event"),
			"Rack's recursePositionEvent: visible children containing pos only, with pos in the child's coordinates.");

	mod.def("get_key_name", [](int key) {
		runtime::requireMainThread("naming a key");
		return rack::widget::getKeyName(key);
	}, py::arg("key"), "Rack's display name for a GLFW key code; empty for control codes.");
	mod.def("get_key_command_name", [](int key, int mods) {
		runtime::requireMainThread("naming a key command");
		return rack::widget::getKeyCommandName(key, mods);
	}, py::arg("key"), py::arg("mods"), "Rack's display name for a key with modifiers, such as Ctrl+S.");
}
} }  // namespace rackpy::widgets
