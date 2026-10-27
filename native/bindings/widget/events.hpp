// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../support/overrides.hpp"
#include "managed.hpp"
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>

// Canonical Widget event/handler names. Prototype records are bound separately.
#define RACKPY_WIDGET_EVENTS(X) \
 X(HoverEvent, onHover, on_hover) \
 X(ButtonEvent, onButton, on_button) \
 X(DoubleClickEvent, onDoubleClick, on_double_click) \
 X(HoverKeyEvent, onHoverKey, on_hover_key) \
 X(HoverTextEvent, onHoverText, on_hover_text) \
 X(HoverScrollEvent, onHoverScroll, on_hover_scroll) \
 X(EnterEvent, onEnter, on_enter) \
 X(LeaveEvent, onLeave, on_leave) \
 X(SelectEvent, onSelect, on_select) \
 X(DeselectEvent, onDeselect, on_deselect) \
 X(SelectKeyEvent, onSelectKey, on_select_key) \
 X(SelectTextEvent, onSelectText, on_select_text) \
 X(DragStartEvent, onDragStart, on_drag_start) \
 X(DragEndEvent, onDragEnd, on_drag_end) \
 X(DragMoveEvent, onDragMove, on_drag_move) \
 X(DragHoverEvent, onDragHover, on_drag_hover) \
 X(DragEnterEvent, onDragEnter, on_drag_enter) \
 X(DragLeaveEvent, onDragLeave, on_drag_leave) \
 X(DragDropEvent, onDragDrop, on_drag_drop) \
 X(PathDropEvent, onPathDrop, on_path_drop) \
 X(ActionEvent, onAction, on_action) \
 X(ChangeEvent, onChange, on_change) \
 X(DirtyEvent, onDirty, on_dirty) \
 X(RepositionEvent, onReposition, on_reposition) \
 X(ResizeEvent, onResize, on_resize) \
 X(AddEvent, onAdd, on_add) \
 X(RemoveEvent, onRemove, on_remove) \
 X(ShowEvent, onShow, on_show) \
 X(HideEvent, onHide, on_hide) \
 X(ContextCreateEvent, onContextCreate, on_context_create) \
 X(ContextDestroyEvent, onContextDestroy, on_context_destroy)

namespace rackpy { namespace widgets {
struct Scope {
	bool active = true;
	std::shared_ptr<runtime::ContextToken> context;
	std::function<void()> validate;
	void check() const {
		runtime::requireMainThread("using Widget callback data");
		if (!active) throw runtime::StaleHandle("Widget callback scope has ended");
		if (context) runtime::checkContext(*context);
		if (validate) validate();
	}
};
struct ScopeEnd {
	std::shared_ptr<Scope> scope;
	~ScopeEnd() { scope->active = false; }
};
/** The innermost drawing callback running on the main thread (nullptr outside drawing). */
inline Scope*& currentDrawing() {
	static Scope* current = nullptr;
	return current;
}
/** A drawing callback's NanoVG state: its context, and the nvgSave calls Python has not restored yet. */
struct DrawState {
	NVGcontext* vg = nullptr;
	int saves = 0;
	Scope* enclosing = nullptr;
};
inline std::unordered_map<const Scope*, DrawState>& drawStates() {
	static std::unordered_map<const Scope*, DrawState> states;
	return states;
}
/** Makes a callback scope the innermost drawing scope. At its end, restores the NanoVG
states Python saved and did not restore, so Rack's own nvgSave/nvgRestore pairs stay matched. */
struct DrawScope {
	std::shared_ptr<Scope> scope;
	DrawScope(std::shared_ptr<Scope> s, NVGcontext* vg) : scope(std::move(s)) {
		auto& state = drawStates()[scope.get()];
		state.vg = vg;
		state.enclosing = currentDrawing();
		currentDrawing() = scope.get();
	}
	~DrawScope() {
		auto it = drawStates().find(scope.get());
		if (it == drawStates().end()) return;
		for (; it->second.saves > 0; --it->second.saves)
			nvgRestore(it->second.vg);
		currentDrawing() = it->second.enclosing;
		drawStates().erase(it);
	}
	DrawScope(const DrawScope&) = delete;
	DrawScope& operator=(const DrawScope&) = delete;
};
struct Graphics {
	std::shared_ptr<Scope> scope;
	NVGcontext* vg;
	NVGcontext* get() const { scope->check(); return vg; }
	/** For drawing: only in its own callback while it is the innermost one, so nested
	Rack drawing (drawChild's saved states) cannot be disturbed from an enclosing callback. */
	NVGcontext* draw() const {
		scope->check();
		auto it = drawStates().find(scope.get());
		if (it == drawStates().end() || it->second.vg != vg)
			throw runtime::Ownership("this GraphicsContext is not a drawing callback's (draw, draw_layer or draw_vg)");
		if (currentDrawing() != scope.get())
			throw runtime::Ownership("this GraphicsContext belongs to an enclosing drawing callback; draw with the "
				"innermost callback's GraphicsContext");
		return vg;
	}
	DrawState& state() const { draw(); return drawStates()[scope.get()]; }
};
struct Framebuffer {
	std::shared_ptr<Scope> scope;
	NVGLUframebuffer* fb;
	NVGLUframebuffer* get() const { scope->check(); return fb; }
};
struct ContextStorage {
	rack::widget::EventContext value;
	Target target;
};
struct ContextRef {
	std::shared_ptr<Scope> scope;
	rack::widget::EventContext* ptr;
	std::shared_ptr<ContextStorage> owner;
	rack::widget::EventContext& get() const { scope->check(); return *ptr; }
};
struct EventData {
	std::shared_ptr<Scope> scope;
	std::shared_ptr<void> owner;
	const void* ptr = nullptr;
	std::string kind;
	std::function<py::object(const std::string&)> read;
	std::function<void(const std::string&, py::handle)> write;
	std::function<const rack::widget::BaseEvent&()> base;
	std::function<bool(int, int)> keyCommand;
	std::shared_ptr<ContextStorage> contextOwner;
	std::unordered_map<std::string, std::shared_ptr<Scope>> resources;
	std::unordered_map<std::string, Target> widgetTargets;
	void checkTargets() const { for (const auto& field : widgetTargets) field.second.get(); }
	void check() const {
		scope->check();
		for (const auto& resource : resources) resource.second->check();
	}
};
using EventRef = std::shared_ptr<EventData>;

py::object eventObject(const EventRef& ref);
EventRef eventData(py::handle object);
void bindWidgetEvents(py::module_ mod);
void captureEventTarget(const rack::widget::EventContext* context, bool force = false);
W* eventTarget(const rack::widget::EventContext* context);

// Defined in events.cpp for every public event record.
template<class E> EventRef borrowedEvent(const E& e, const char* kind, const std::shared_ptr<Scope>& scope);

template<class Base, class E, class F>
void trampolineEvent(const Base* self, const std::shared_ptr<State>& state, const char* name,
	const char* kind, const E& e, F fallback) noexcept {
	if (!runtime::isMainThread()) return;
	// Keep this outside the GIL lambda: get_override() can hold the last
	// Python owner until its bound-method temporary is destroyed.
	CallbackGuard dispatch;
	bool answered = false;
	runtime::callPython(name, [&] {
		checked(state);
		if (auto f = rackpy::bindings::pythonOverride(self, name)) {
			auto scope = std::make_shared<Scope>();
			scope->context = state->token->context;
			ScopeEnd end{scope};
			CallbackGuard guard;
			answered = true;
			f(eventObject(borrowedEvent(e, kind, scope)));
		}
	});
	if (!answered) {
		try { CallbackGuard guard; fallback(); }
		catch (...) { runtime::detail::noteRaised(name, "native Widget fallback failed"); }
	}
}
} }
