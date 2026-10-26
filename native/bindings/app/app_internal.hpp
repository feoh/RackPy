// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by rackpy.raw.app's controls (app.cpp) and module widgets (module_widget.cpp).
#pragma once
#include <rack.hpp>

#include "../widget/variants.hpp"

namespace rackpy {
namespace widgets {
namespace appdetail {

/** Remember the context menu Rack built for a Python control or module widget: its
items point at the widget, so RackPy closes it when the widget dies. */
void trackMenu(const std::shared_ptr<State>& s, rack::ui::Menu* menu, rack::app::ParamWidget* touched);
/** A port its module widget's Python factory created: it takes part in cables. */
void markFactoryPort(const std::shared_ptr<State>& s);

/** Rack calls appendContextMenu last in createContextMenu, with the menu it built. */
template <class Base, class Self>
void appendMenu(Self* self, rack::ui::Menu* menu, rack::app::ParamWidget* touched) {
	if (!runtime::isMainThread())
		return;
	try {
		trackMenu(self->lifetime, menu, touched);
	}
	catch (...) {
		runtime::detail::noteRaised("append_context_menu", "could not track the widget's context menu");
	}
	AppendScope scope(menu);
	CallbackGuard dispatch;
	bool answered = false;
	runtime::callPython("append_context_menu", [&] {
		checked(self->lifetime);
		if (auto f = py::get_override(static_cast<const Base*>(self), "append_context_menu")) {
			CallbackGuard guard;
			answered = true;
			f(wrapper(menu));
		}
	});
	if (!answered) {
		try {
			CallbackGuard guard;
			self->Base::appendContextMenu(menu);
		}
		catch (...) {
			runtime::detail::noteRaised("append_context_menu", "native Widget fallback failed");
		}
	}
}

/** A Rack-made or Python menu argument (rackpy.raw.ui.Menu). */
rack::ui::Menu* menuArgument(py::handle value);

/** Rack's cable invariants: a cabled port lives inside its module's ModuleWidget (removing the
widget disconnects it), and that widget's factory created the port (Rack's undo and patch
loading find ports through ModuleWidget::getInput/getOutput and throw if a port is missing). */
bool cableSafe(rack::app::PortWidget& port);

template <class Base>
struct ParamWidgetT : Widget<Base> {
	void initParamQuantity() override {
		this->pyVoid("init_param_quantity", [this] { Base::initParamQuantity(); });
	}
	void appendContextMenu(rack::ui::Menu* menu) override { appendMenu<Base>(this, menu, this); }
};

template <class Base>
struct PortWidgetT : Widget<Base> {
	void appendContextMenu(rack::ui::Menu* menu) override { appendMenu<Base>(this, menu, nullptr); }
	// Python overrides still run; outside its module's ModuleWidget, Rack's cable behaviour does not.
	void onDragStart(const W::DragStartEvent& e) override {
		trampolineEvent(static_cast<const Base*>(this), this->lifetime, "on_drag_start", "DragStartEvent", e,
			[this, &e] { if (cableSafe(*this)) Base::onDragStart(e); });
	}
	void onDragEnter(const W::DragEnterEvent& e) override {
		trampolineEvent(static_cast<const Base*>(this), this->lifetime, "on_drag_enter", "DragEnterEvent", e,
			[this, &e] { if (cableSafe(*this)) Base::onDragEnter(e); });
	}
	void onDragDrop(const W::DragDropEvent& e) override {
		trampolineEvent(static_cast<const Base*>(this), this->lifetime, "on_drag_drop", "DragDropEvent", e,
			[this, &e] { if (cableSafe(*this)) Base::onDragDrop(e); });
	}
};

/** Python port classes: their event base methods keep the cable invariant too. */
template <class T, class... Options>
void guardPortEvents(py::classh<T, Options...>& cls) {
#define GUARD(E, method, python) \
	cls.attr(#python) = py::cpp_function([](py::object self, py::object event) { \
		checkTraversal(state(self)); \
		auto& w = native<T>(self); \
		auto ref = eventData(event); \
		const auto& e = nativeEvent<W::E>(ref, #E); \
		if (!cableSafe(w)) \
			return; \
		CallbackGuard guard; \
		w.T::method(e); \
		captureEventTarget(e.context); \
	}, py::name(#python), py::is_method(cls), py::arg("event"), \
		"Rack's behaviour, which takes part in cables only inside this port's module's ModuleWidget.");
	GUARD(DragStartEvent, onDragStart, on_drag_start)
	GUARD(DragEnterEvent, onDragEnter, on_drag_enter)
	GUARD(DragDropEvent, onDragDrop, on_drag_drop)
#undef GUARD
}

template <class T, class... Options>
void paramMethods(py::classh<T, Options...>& cls) {
	cls.def("init_param_quantity", [](py::object self) { native<T>(self).T::initParamQuantity(); },
			"Rack's configuration of the ParamQuantity for this kind of control (e.g. a Knob enables smoothing).")
		.def("append_context_menu", [](py::object self, py::object menu) { native<T>(self).T::appendContextMenu(menuArgument(menu)); },
			py::arg("menu"), "Rack's default adds nothing. Override to add items; Rack passes the menu it is building.");
}



/** rackpy.raw.app.ModuleWidget (module_widget.cpp). */
void bindModuleWidget(py::module_ app);
/** rackpy.raw.componentlibrary's native widgets (components.cpp). */
void bindComponentLibrary(py::module_ raw);

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
