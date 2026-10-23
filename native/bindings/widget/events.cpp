// SPDX-License-Identifier: GPL-3.0-or-later
#include "events.hpp"
#include <pybind11/stl.h>
#include <unordered_map>
#include "rackpy/runtime/interpreter.hpp"

namespace rackpy { namespace widgets {
namespace {
runtime::PyRef types;
std::unordered_map<const rack::widget::EventContext*, std::weak_ptr<ContextStorage>> contexts;
std::shared_ptr<ContextStorage> contextStorage(const rack::widget::EventContext* ptr) {
	auto it = contexts.find(ptr);
	return it == contexts.end() ? nullptr : it->second.lock();
}
py::object contextObject(const ContextRef& ref) {
	return types.get()["EventContext"](py::arg("_ref") = py::cast(ref));
}

#define HAS_MEMBER(member) \
 template<class E, class = void> struct has_##member : std::false_type {}; \
 template<class E> struct has_##member<E, std::void_t<decltype(std::declval<E>().member)>> : std::true_type {};
HAS_MEMBER(pos)
HAS_MEMBER(mouseDelta)
HAS_MEMBER(scrollDelta)
HAS_MEMBER(button)
HAS_MEMBER(action)
HAS_MEMBER(mods)
HAS_MEMBER(key)
HAS_MEMBER(scancode)
HAS_MEMBER(keyName)
HAS_MEMBER(codepoint)
HAS_MEMBER(origin)
HAS_MEMBER(paths)
HAS_MEMBER(vg)
HAS_MEMBER(fb)
HAS_MEMBER(clipBox)
#undef HAS_MEMBER

template<class E> struct Store { E event{}; };
template<> struct Store<W::PathDropEvent> {
	std::vector<std::string> paths;
	W::PathDropEvent event{paths};
};

template<class E>
EventRef make(const E& event, const char* kind, const std::shared_ptr<Scope>& scope, std::shared_ptr<void> owner = {}) {
	auto ref = std::make_shared<EventData>();
	ref->scope = scope;
	ref->ptr = &event;
	ref->kind = kind;
	ref->owner = std::move(owner);
	if constexpr (has_origin<E>::value) ref->widgetTargets.emplace("origin", target(event.origin));
	std::weak_ptr<EventData> weak = ref;
	ref->read = [&event, scope, weak](const std::string& name) -> py::object {
#define READ(member, python) if constexpr (has_##member<E>::value) { if (name == python) return py::cast(event.member, py::return_value_policy::copy); }
		READ(pos, "pos") READ(mouseDelta, "mouse_delta") READ(scrollDelta, "scroll_delta")
		READ(button, "button") READ(action, "action") READ(mods, "mods") READ(key, "key")
		READ(scancode, "scancode") READ(keyName, "key_name") READ(codepoint, "codepoint")
		READ(paths, "paths") READ(clipBox, "clip_box")
#undef READ
		if constexpr (has_origin<E>::value) if (name == "origin") {
			auto held = weak.lock();
			if (!held) throw runtime::StaleHandle("event record has expired");
			return wrapper(held->widgetTargets.at("origin").get());
		}
		if constexpr (has_vg<E>::value) if (name == "vg")
			return event.vg ? py::cast(Graphics{scope, event.vg}) : py::none();
		if constexpr (has_fb<E>::value) if (name == "fb")
			return event.fb ? py::cast(Framebuffer{scope, event.fb}) : py::none();
		if constexpr (std::is_base_of<rack::widget::BaseEvent, E>::value) {
			if (name == "context")
				return event.context ? contextObject(ContextRef{scope, event.context, contextStorage(event.context)}) : py::none();
		}
		throw py::attribute_error("no event field named " + name);
	};
	ref->write = [&event, weak](const std::string& name, py::handle value) {
		auto held = weak.lock();
		if (!held || !held->owner)
			throw py::type_error("Rack callback arguments are const; copy values or change the shared EventContext");
		auto& e = const_cast<E&>(event);
#define WRITE(member, python) if constexpr (has_##member<E>::value) { if (name == python) { e.member = value.cast<std::decay_t<decltype(e.member)>>(); return; } }
		WRITE(pos, "pos") WRITE(mouseDelta, "mouse_delta") WRITE(scrollDelta, "scroll_delta")
		WRITE(button, "button") WRITE(action, "action") WRITE(mods, "mods") WRITE(key, "key")
		WRITE(scancode, "scancode") WRITE(keyName, "key_name") WRITE(codepoint, "codepoint")
		WRITE(clipBox, "clip_box")
#undef WRITE
		if constexpr (has_paths<E>::value) if (name == "paths") {
			const_cast<std::vector<std::string>&>(e.paths) = value.cast<std::vector<std::string>>(); return;
		}
		if constexpr (has_origin<E>::value) if (name == "origin") {
			auto* ptr = argument(value);
			auto locator = target(ptr);
			held->widgetTargets.at("origin") = std::move(locator);
			e.origin = ptr;
			return;
		}
		if constexpr (has_vg<E>::value) if (name == "vg") {
			if (value.is_none()) { e.vg = nullptr; held->resources.erase("vg"); return; }
			auto graphics = value.cast<Graphics>();
			auto* ptr = graphics.get();
			held->resources["vg"] = graphics.scope;
			e.vg = ptr;
			return;
		}
		if constexpr (has_fb<E>::value) if (name == "fb") {
			if (value.is_none()) { e.fb = nullptr; held->resources.erase("fb"); return; }
			auto framebuffer = value.cast<Framebuffer>();
			auto* ptr = framebuffer.get();
			held->resources["fb"] = framebuffer.scope;
			e.fb = ptr;
			return;
		}
		if constexpr (std::is_base_of<rack::widget::BaseEvent, E>::value) {
			if (name == "context") {
				if (value.is_none()) { e.context = nullptr; held->contextOwner.reset(); held->resources.erase("context"); return; }
				auto context = value.attr("_ref").cast<ContextRef>();
				auto* ptr = &context.get();
				held->resources["context"] = context.scope;
				held->contextOwner = context.owner;
				e.context = ptr;
				return;
			}
		}
		throw py::attribute_error("no writable event field named " + name);
	};
	if constexpr (std::is_base_of<rack::widget::BaseEvent, E>::value)
		ref->base = [&event]() -> const rack::widget::BaseEvent& { return event; };
	if constexpr (std::is_base_of<W::KeyBaseEvent, E>::value)
		ref->keyCommand = [&event](int key, int mods) { return event.isKeyCommand(key, mods); };
	return ref;
}

template<class E> EventRef owned(const char* kind) {
	runtime::requireMainThread("constructing Widget event data");
	auto store = std::make_shared<Store<E>>();
	return make(store->event, kind, std::make_shared<Scope>(), store);
}

const rack::widget::BaseEvent& base(const EventRef& ref) {
	ref->check();
	if (!ref->base) throw py::type_error("this record is not a BaseEvent");
	return ref->base();
}
}

template<class E> EventRef borrowedEvent(const E& e, const char* kind, const std::shared_ptr<Scope>& scope) {
	return make(e, kind, scope);
}
#define INSTANTIATE(E, native, python) template EventRef borrowedEvent<W::E>(const W::E&, const char*, const std::shared_ptr<Scope>&);
RACKPY_WIDGET_EVENTS(INSTANTIATE)
#undef INSTANTIATE
// DrawArgs use the same checked field machinery, but have no BaseEvent state.
template EventRef borrowedEvent<W::DrawArgs>(const W::DrawArgs&, const char*, const std::shared_ptr<Scope>&);

void captureEventTarget(const rack::widget::EventContext* context, bool force) {
	if (!context) return;
	auto storage = contextStorage(context);
	if (storage && (force || storage->target.value != context->target)) {
		try { storage->target = target(context->target); }
		catch (...) {
			storage->target = {};
			storage->value.target = nullptr;
			throw;
		}
	}
}
W* eventTarget(const rack::widget::EventContext* context) {
	if (!context) return nullptr;
	captureEventTarget(context);
	auto storage = contextStorage(context);
	return storage ? storage->target.get() : context->target;
}

py::object eventObject(const EventRef& ref) {
	return types.get()[py::str(ref->kind)](py::arg("_ref") = py::cast(ref));
}
EventRef eventData(py::handle object) {
	auto ref = object.attr("_ref").cast<EventRef>();
	ref->check();
	return ref;
}

void bindWidgetEvents(py::module_ mod) {
	py::class_<Graphics>(mod, "GraphicsContext", py::is_final(), "Callback-scoped, opaque NanoVG context (drawing APIs are RP-27).")
		.def_property_readonly("alive", [](const Graphics& g) { g.scope->check(); return g.vg != nullptr; });
	py::class_<Framebuffer>(mod, "Framebuffer", py::is_final(), "Opaque, checked framebuffer resource; never an integer address.")
		.def_property_readonly("alive", [](const Framebuffer& f) { return f.get() != nullptr; });
	py::class_<ContextRef>(mod, "_EventContextRef")
		.def("get", [](const ContextRef& ref, const std::string& field) -> py::object {
			auto& c = ref.get();
			if (field == "propagating") return py::bool_(c.propagating);
			if (field == "consumed") return py::bool_(c.consumed);
			if (field == "target") return wrapper(eventTarget(&c));
			throw py::attribute_error(field);
		})
		.def("set", [](const ContextRef& ref, const std::string& field, py::handle value) {
			auto& c = ref.get();
			if (field == "propagating") c.propagating = value.cast<bool>();
			else if (field == "consumed") c.consumed = value.cast<bool>();
			else if (field == "target") {
				auto* ptr = argument(value);
				auto locator = target(ptr);
				if (ref.owner) ref.owner->target = std::move(locator);
				c.target = ptr;
			}
			else throw py::attribute_error(field);
		});
	py::class_<EventData, EventRef>(mod, "_EventRef")
		.def("get", [](const EventRef& ref, const std::string& field) { ref->check(); return ref->read(field); })
		.def("set", [](const EventRef& ref, const std::string& field, py::handle value) { ref->check(); ref->write(field, value); })
		.def("is_propagating", [](const EventRef& ref) { return base(ref).isPropagating(); })
		.def("is_consumed", [](const EventRef& ref) { return base(ref).isConsumed(); })
		.def("get_target", [](const EventRef& ref) { return wrapper(eventTarget(base(ref).context)); })
		.def("stop_propagating", [](const EventRef& ref) { base(ref).stopPropagating(); })
		.def("unconsume", [](const EventRef& ref) { base(ref).unconsume(); })
		.def("consume", [](const EventRef& ref, py::handle target) { base(ref).consume(argument(target)); captureEventTarget(base(ref).context, true); })
		.def("set_target", [](const EventRef& ref, py::handle target) { base(ref).setTarget(argument(target)); captureEventTarget(base(ref).context, true); })
		.def("is_key_command", [](const EventRef& ref, int key, int mods) {
			ref->check();
			if (!ref->keyCommand) throw py::type_error("not a key event");
			return ref->keyCommand(key, mods);
		});
	mod.def("_make_event_context", [] {
		runtime::requireMainThread("constructing EventContext");
		auto owner = std::make_shared<ContextStorage>();
		contexts[&owner->value] = owner;
		return ContextRef{std::make_shared<Scope>(), &owner->value, owner};
	});
	mod.def("_make_event", [](const std::string& kind) -> EventRef {
#define OWN(E, native, python) if (kind == #E) return owned<W::E>(#E);
		RACKPY_WIDGET_EVENTS(OWN)
#undef OWN
		if (kind == "BaseEvent") return owned<rack::widget::BaseEvent>("BaseEvent");
		if (kind == "PositionBaseEvent") return owned<W::PositionBaseEvent>("PositionBaseEvent");
		if (kind == "KeyBaseEvent") return owned<W::KeyBaseEvent>("KeyBaseEvent");
		if (kind == "TextBaseEvent") return owned<W::TextBaseEvent>("TextBaseEvent");
		if (kind == "DragBaseEvent") return owned<W::DragBaseEvent>("DragBaseEvent");
		if (kind == "DrawArgs") return owned<W::DrawArgs>("DrawArgs");
		throw py::value_error("unknown Widget event record: " + kind);
	});
	mod.def("_register_event_types", [](py::dict classes) { types = runtime::PyRef(classes); });
	runtime::onFinalize([] { types.reset(); contexts.clear(); });
}
} }
