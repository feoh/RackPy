// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "events.hpp"
#include "drawing.hpp"

namespace rackpy { namespace widgets {

template<class Base> Base& native(py::handle self) {
	auto* value = dynamic_cast<Base*>(&checked(state(self)));
	if (!value) throw py::type_error("wrong native Widget base for this method");
	return *value;
}

template<class E> const E& nativeEvent(const EventRef& ref, const char* kind) {
	ref->check();
	ref->checkTargets();
	if (ref->kind != kind) throw py::type_error(std::string("expected ") + kind);
	if constexpr (std::is_base_of<rack::widget::BaseEvent, E>::value)
		eventTarget(static_cast<const E*>(ref->ptr)->context);
	return *static_cast<const E*>(ref->ptr);
}

inline EventRef drawArgs(py::handle object) {
	auto data = eventData(object);
	const auto& args = nativeEvent<W::DrawArgs>(data, "DrawArgs");
	if (!args.vg) throw py::value_error("drawing requires a live callback GraphicsContext");
	return data;
}

template<class Base, class... Options>
void bindBehavior(py::classh<Base, Options...>& cls) {
	cls.def("step", [](py::object self) {
		auto owner = state(self);
		checkTraversal(owner);
		checkStep(owner);
		CallbackGuard guard;
		native<Base>(self).Base::step();
	})
	.def("get_children_bounding_box", [](py::object self) { checkTraversal(state(self)); return native<Base>(self).Base::getChildrenBoundingBox(); })
	.def("get_visible_children_bounding_box", [](py::object self) { checkTraversal(state(self)); return native<Base>(self).Base::getVisibleChildrenBoundingBox(); })
	.def("get_relative_offset", [](py::object self, rack::math::Vec value, py::object ancestor) {
		return native<Base>(self).Base::getRelativeOffset(value, argument(ancestor));
	}, py::arg("value"), py::arg("ancestor"))
	.def("get_relative_zoom", [](py::object self, py::object ancestor) {
		return native<Base>(self).Base::getRelativeZoom(argument(ancestor));
	}, py::arg("ancestor"))
	.def("get_viewport", [](py::object self, rack::math::Rect rect) { return native<Base>(self).Base::getViewport(rect); },
		py::arg("rect") = rack::math::Rect::inf())
	.def("draw", [](py::object self, py::object args) {
		auto owner = state(self);
		checkTraversal(owner);
		auto heldArgs = drawArgs(args);
		const auto& value = *static_cast<const W::DrawArgs*>(heldArgs->ptr);
		++owner->graphicsGeneration;
		DrawGuard drawing(owner);
		CallbackGuard guard;
		auto& w = native<Base>(self);
		validateNativeDraw(w, value);
		w.Base::draw(value);
	}, py::arg("args"))
	.def("draw_vg", [](py::object self, const Graphics& vg) {
		auto& w = native<Base>(self);
		legacyDraw(w, vg.get());
	}, py::arg("graphics"))
	.def("draw_layer", [](py::object self, py::object args, int layer) {
		auto owner = state(self);
		checkTraversal(owner);
		auto heldArgs = drawArgs(args);
		const auto& value = *static_cast<const W::DrawArgs*>(heldArgs->ptr);
		++owner->graphicsGeneration;
		DrawGuard drawing(owner);
		CallbackGuard guard;
		native<Base>(self).Base::drawLayer(value, layer);
	}, py::arg("args"), py::arg("layer"));
#define BIND_EVENT(E, method, python) \
	cls.def(#python, [](py::object self, py::object event) { \
		checkTraversal(state(self)); \
		auto& w = native<Base>(self); \
		auto ref = eventData(event); \
		const auto& e = nativeEvent<W::E>(ref, #E); \
		if constexpr (std::is_same<W::E, W::ContextCreateEvent>::value || std::is_same<W::E, W::ContextDestroyEvent>::value) { \
			auto owner = state(self); \
			if (owner->drawing) throw runtime::Ownership("cannot change a graphics context during drawing"); \
			++owner->graphicsGeneration; \
		} \
		CallbackGuard guard; \
		w.Base::method(e); \
		captureEventTarget(e.context); \
	}, py::arg("event"));
	RACKPY_WIDGET_EVENTS(BIND_EVENT)
#undef BIND_EVENT
}
} }
