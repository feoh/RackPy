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

/** rackpy.raw.app.ModuleWidget (module_widget.cpp). */
void bindModuleWidget(py::module_ app);

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
