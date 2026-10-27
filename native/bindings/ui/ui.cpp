// SPDX-License-Identifier: GPL-3.0-or-later
// rack::ui -> rackpy.raw.ui (RP-26A): Rack's UI widgets as subclassable Python
// classes on RP-25's managed Widget machinery. Main thread only; Python
// overrides are contained; each widget has one native owner.
#include "../support/overrides.hpp"
#include <rack.hpp>

#include "../widget/variants.hpp"
#include "ui.hpp"
#include "../history/history.hpp"

// As managed.hpp does for rack::widget: keep the registered SDK-base address.
namespace pybind11 {
#define RACKPY_UI_ADDRESS(T) \
	template <> \
	struct polymorphic_type_hook<rack::ui::T> { \
		static const void* get(const rack::ui::T* ptr, const std::type_info*& type) { \
			type = &typeid(rack::ui::T); \
			return ptr; \
		} \
	};
RACKPY_UI_ADDRESS(Label)
RACKPY_UI_ADDRESS(Button)
RACKPY_UI_ADDRESS(ChoiceButton)
RACKPY_UI_ADDRESS(RadioButton)
RACKPY_UI_ADDRESS(OptionButton)
RACKPY_UI_ADDRESS(Slider)
RACKPY_UI_ADDRESS(ProgressBar)
RACKPY_UI_ADDRESS(Tooltip)
RACKPY_UI_ADDRESS(TooltipOverlay)
RACKPY_UI_ADDRESS(List)
RACKPY_UI_ADDRESS(SequentialLayout)
RACKPY_UI_ADDRESS(Scrollbar)
RACKPY_UI_ADDRESS(ScrollWidget)
RACKPY_UI_ADDRESS(TextField)
RACKPY_UI_ADDRESS(PasswordField)
RACKPY_UI_ADDRESS(MenuOverlay)
RACKPY_UI_ADDRESS(Menu)
RACKPY_UI_ADDRESS(MenuEntry)
RACKPY_UI_ADDRESS(MenuItem)
RACKPY_UI_ADDRESS(ColorDotMenuItem)
RACKPY_UI_ADDRESS(MenuLabel)
RACKPY_UI_ADDRESS(MenuSeparator)
#undef RACKPY_UI_ADDRESS
}  // namespace pybind11

namespace rackpy {
namespace widgets {

namespace {

namespace ui = rack::ui;
using Q = rack::Quantity;
using OW = rack::widget::OpaqueWidget;

/** A Python Menu returned to Rack from a factory: Rack owns it from here. */
ui::Menu* handOver(py::object value) {
	if (value.is_none())
		return nullptr;
	auto s = state(value);
	auto& w = checked(s);
	auto* menu = dynamic_cast<ui::Menu*>(&w);
	if (!menu)
		throw py::type_error("create_child_menu must return a rackpy.raw.ui.Menu or None");
	if (w.parent)
		throw runtime::Ownership("create_child_menu must return a new menu that has no parent");
	Owned owned(s->disown(value), UiDelete{s});
	owned.release();
	if (!s->transferred) {
		s->transferred = true;
		runtime::noteTransferred();
	}
	return menu;
}

/** MenuItem's own virtual: Rack asks for a child menu when the pointer enters the item. */
template <class Base>
struct MenuItemWidget : Widget<Base> {
	ui::Menu* createChildMenu() override {
		if (!runtime::isMainThread())
			return nullptr;
		ui::Menu* menu = nullptr;
		bool answered = false;
		runtime::callPython("MenuItem.create_child_menu", [&] {
			checked(this->lifetime);
			if (auto f = rackpy::bindings::pythonOverride(static_cast<const Base*>(this), "create_child_menu")) {
				answered = true;
				FactoryScope factory;  // building the new, detached menu is allowed here
				menu = handOver(f());
			}
		});
		if (answered)
			return menu;
		return Base::createChildMenu();
	}
};

template <class Base>
struct TextFieldWidget : Widget<Base> {
	int getTextPosition(rack::math::Vec mousePos) override {
		return this->template result<int>("get_text_position", [this, mousePos] { return Base::getTextPosition(mousePos); },
			mousePos);
	}
};

/** A not-owned Quantity* field: the widget keeps the Python Quantity alive while it points at it. */
template <class T, class... Options>
void quantityField(py::classh<T, Options...>& cls) {
	cls.def_property("quantity", [](py::object self) -> py::object {
			Q* q = native<T>(self).quantity;
			return q ? py::cast(q, py::return_value_policy::reference) : py::none();
		},
		[](py::object self, py::object value) {
			T& w = native<T>(self);
			if (!value.is_none() && !py::isinstance<Q>(value))
				throw py::type_error("quantity must be a rackpy.raw.Quantity (e.g. rackpy.types.ValueQuantity) or None");
			Q* q = value.is_none() ? nullptr : value.cast<Q*>();
			state(self)->keep("quantity", value);
			w.quantity = q;
		}, "Not owned by Rack: the widget keeps this Quantity (a rackpy.raw.Quantity) alive while it refers to it.");
}

/** TextField::prevField/nextField: a widget Rack focuses with Tab. Only a widget
RackPy manages, so that deleting it clears the field instead of leaving Rack a
dangling pointer. */
void focusField(py::classh<ui::TextField, OW, TextFieldWidget<ui::TextField>>& cls, const char* name,
	W* ui::TextField::*field) {
	cls.def_property(name, [field](py::object self) { return wrapper(native<ui::TextField>(self).*field); },
		[name, field](py::object self, py::object value) {
			auto& tf = native<ui::TextField>(self);
			if (value.is_none()) {
				tf.*field = nullptr;
				return;
			}
			auto target = state(value);
			W* w = &checked(target);
			std::weak_ptr<State> source = state(self);
			target->onDestroyed.push_back([source, field, w] {
				auto s = source.lock();
				if (!s || !s->native)
					return;
				if (auto* owner = dynamic_cast<ui::TextField*>(s->native))
					if (owner->*field == w)
						owner->*field = nullptr;
			});
			tf.*field = w;
		}, "A RackPy widget Tab moves focus to (cleared if that widget is deleted), or None.");
}

}  // namespace

void bindUi(py::module_ raw) {
	py::module_ glfw = raw.def_submodule("glfw", "GLFW input constants used by Rack events.");
	py::module_ blendish = raw.def_submodule("blendish", "Blendish corner flags used by Rack menus.");
#include "../generated/boundary_constants.inc"
	py::module_ mod = raw.def_submodule("ui", "rack::ui: Rack's UI widgets, menus and theme (RP-26A).");

	// --- Label --------------------------------------------------------------------
	auto label = variant<ui::Label>(mod, "Label");
	py::enum_<ui::Label::Alignment>(label, "Alignment")
		.value("LEFT_ALIGNMENT", ui::Label::LEFT_ALIGNMENT)
		.value("CENTER_ALIGNMENT", ui::Label::CENTER_ALIGNMENT)
		.value("RIGHT_ALIGNMENT", ui::Label::RIGHT_ALIGNMENT)
		.export_values();
	label.def_property("text", [](py::object self) { return native<ui::Label>(self).text; },
			[](py::object self, std::string text) { native<ui::Label>(self).text = std::move(text); })
		.def_property("font_size", [](py::object self) { return native<ui::Label>(self).fontSize; },
			[](py::object self, float v) { native<ui::Label>(self).fontSize = v; })
		.def_property("line_height", [](py::object self) { return native<ui::Label>(self).lineHeight; },
			[](py::object self, float v) { native<ui::Label>(self).lineHeight = v; })
		.def_property("color", [](py::object self) { return native<ui::Label>(self).color; },
			[](py::object self, NVGcolor c) { native<ui::Label>(self).color = c; })
		.def_property("alignment", [](py::object self) { return native<ui::Label>(self).alignment; },
			[](py::object self, ui::Label::Alignment a) { native<ui::Label>(self).alignment = a; });

	// --- Buttons --------------------------------------------------------------------
	auto button = variant<ui::Button, OW>(mod, "Button");
	button.def_property("text", [](py::object self) { return native<ui::Button>(self).text; },
		[](py::object self, std::string text) { native<ui::Button>(self).text = std::move(text); });
	quantityField(button);
	variant<ui::ChoiceButton, ui::Button>(mod, "ChoiceButton");
	variant<ui::RadioButton, ui::Button>(mod, "RadioButton");
	variant<ui::OptionButton, ui::RadioButton>(mod, "OptionButton");

	// --- Slider, ProgressBar, Tooltip ---------------------------------------------------
	auto slider = variant<ui::Slider, OW>(mod, "Slider");
	quantityField(slider);
	auto progress = variant<ui::ProgressBar>(mod, "ProgressBar");
	quantityField(progress);
	variant<ui::Tooltip>(mod, "Tooltip")
		.def_property("text", [](py::object self) { return native<ui::Tooltip>(self).text; },
			[](py::object self, std::string text) { native<ui::Tooltip>(self).text = std::move(text); });
	variant<ui::TooltipOverlay, rack::widget::TransparentWidget>(mod, "TooltipOverlay");

	// --- Layout -----------------------------------------------------------------------
	variant<ui::List, OW>(mod, "List");
	using SL = ui::SequentialLayout;
	auto layout = variant<SL>(mod, "SequentialLayout");
	py::enum_<SL::Orientation>(layout, "Orientation")
		.value("HORIZONTAL_ORIENTATION", SL::HORIZONTAL_ORIENTATION)
		.value("VERTICAL_ORIENTATION", SL::VERTICAL_ORIENTATION)
		.export_values();
	py::enum_<SL::Alignment>(layout, "Alignment")
		.value("LEFT_ALIGNMENT", SL::LEFT_ALIGNMENT)
		.value("CENTER_ALIGNMENT", SL::CENTER_ALIGNMENT)
		.value("RIGHT_ALIGNMENT", SL::RIGHT_ALIGNMENT)
		.value("TOP_ALIGNMENT", SL::TOP_ALIGNMENT)
		.value("MIDDLE_ALIGNMENT", SL::MIDDLE_ALIGNMENT)
		.value("BOTTOM_ALIGNMENT", SL::BOTTOM_ALIGNMENT)
		.export_values();
	layout.def_property("orientation", [](py::object self) { return native<SL>(self).orientation; },
			[](py::object self, SL::Orientation v) { native<SL>(self).orientation = v; })
		.def_property("alignment", [](py::object self) { return native<SL>(self).alignment; },
			[](py::object self, SL::Alignment v) { native<SL>(self).alignment = v; })
		.def_property("wrap", [](py::object self) { return native<SL>(self).wrap; },
			[](py::object self, bool v) { native<SL>(self).wrap = v; })
		.def_property("margin", [](py::object self) { return native<SL>(self).margin; },
			[](py::object self, rack::math::Vec v) { native<SL>(self).margin = v; }, "A copy; assign a Vec to change it.")
		.def_property("spacing", [](py::object self) { return native<SL>(self).spacing; },
			[](py::object self, rack::math::Vec v) { native<SL>(self).spacing = v; }, "A copy; assign a Vec to change it.");

	// --- Scrolling ----------------------------------------------------------------------
	auto scrollbar = variant<ui::Scrollbar, OW>(mod, "Scrollbar");
	scrollbar.def_property("vertical", [](py::object self) { return native<ui::Scrollbar>(self).vertical; },
		[](py::object self, bool v) { native<ui::Scrollbar>(self).vertical = v; });
	using SW = ui::ScrollWidget;
	variant<SW, OW>(mod, "ScrollWidget")
		.def_property_readonly("container", [](py::object self) { return wrapper(native<SW>(self).container); },
			"The child the content goes in, owned by this ScrollWidget: add widgets with container.add_child().")
		.def_property_readonly("horizontal_scrollbar", [](py::object self) { return wrapper(native<SW>(self).horizontalScrollbar); })
		.def_property_readonly("vertical_scrollbar", [](py::object self) { return wrapper(native<SW>(self).verticalScrollbar); })
		.def_property("offset", [](py::object self) { return native<SW>(self).offset; },
			[](py::object self, rack::math::Vec v) { native<SW>(self).offset = v; }, "A copy; assign a Vec to change it.")
		.def_property("container_box", [](py::object self) { return native<SW>(self).containerBox; },
			[](py::object self, rack::math::Rect r) { native<SW>(self).containerBox = r; })
		.def_property("hide_scrollbars", [](py::object self) { return native<SW>(self).hideScrollbars; },
			[](py::object self, bool v) { native<SW>(self).hideScrollbars = v; })
		.def("get_scroll_offset", [](py::object self) { return native<SW>(self).getScrollOffset(); })
		.def("scroll_to", [](py::object self, rack::math::Rect r) { native<SW>(self).scrollTo(r); }, py::arg("r"))
		.def("get_container_offset_bound", [](py::object self) { return native<SW>(self).getContainerOffsetBound(); })
		.def("get_handle_offset", [](py::object self) { return native<SW>(self).getHandleOffset(); })
		.def("get_handle_size", [](py::object self) { return native<SW>(self).getHandleSize(); })
		.def("is_scrolling", [](py::object self) { return native<SW>(self).isScrolling(); });

	// --- Text fields ----------------------------------------------------------------------
	using TF = ui::TextField;
	auto field = variant<TF, OW, TextFieldWidget<TF>>(mod, "TextField");
	field.def_property("text", [](py::object self) { return native<TF>(self).text; },
			[](py::object self, std::string v) { native<TF>(self).text = std::move(v); },
			"The raw field: unlike set_text(), it does not move the cursor or send a change event.")
		.def_property("placeholder", [](py::object self) { return native<TF>(self).placeholder; },
			[](py::object self, std::string v) { native<TF>(self).placeholder = std::move(v); })
		.def_property("password", [](py::object self) { return native<TF>(self).password; },
			[](py::object self, bool v) { native<TF>(self).password = v; })
		.def_property("multiline", [](py::object self) { return native<TF>(self).multiline; },
			[](py::object self, bool v) { native<TF>(self).multiline = v; })
		.def_property("cursor", [](py::object self) { return native<TF>(self).cursor; },
			[](py::object self, int v) {
				auto& tf = native<TF>(self);
				tf.cursor = rack::math::clamp(v, 0, (int) tf.text.size());  // Rack indexes text with it
			}, "Clamped to the text: Rack indexes the text with it.")
		.def_property("selection", [](py::object self) { return native<TF>(self).selection; },
			[](py::object self, int v) {
				auto& tf = native<TF>(self);
				tf.selection = rack::math::clamp(v, 0, (int) tf.text.size());
			}, "Clamped to the text.")
		.def("get_text_position", [](py::object self, rack::math::Vec pos) { return native<TF>(self).TF::getTextPosition(pos); },
			py::arg("mouse_pos"))
		.def("get_text", [](py::object self) { return native<TF>(self).getText(); })
		.def("set_text", [](py::object self, std::string text) { checkTraversal(state(self)); native<TF>(self).setText(std::move(text)); },
			py::arg("text"))
		.def("select_all", [](py::object self) { native<TF>(self).selectAll(); })
		.def("get_selected_text", [](py::object self) { return native<TF>(self).getSelectedText(); })
		.def("insert_text", [](py::object self, std::string text) { native<TF>(self).insertText(std::move(text)); },
			py::arg("text"))
		.def("copy_clipboard", [](py::object self) { native<TF>(self).copyClipboard(); })
		.def("cut_clipboard", [](py::object self) { native<TF>(self).cutClipboard(); })
		.def("paste_clipboard", [](py::object self) { native<TF>(self).pasteClipboard(); })
		.def("cursor_to_prev_word", [](py::object self) { native<TF>(self).cursorToPrevWord(); })
		.def("cursor_to_next_word", [](py::object self) { native<TF>(self).cursorToNextWord(); })
		.def("cursor_to_line_start", [](py::object self) { native<TF>(self).cursorToLineStart(); })
		.def("cursor_to_line_end", [](py::object self) { native<TF>(self).cursorToLineEnd(); })
		.def("create_context_menu", [](py::object self) { checkMutation(); native<TF>(self).createContextMenu(); },
			"Rack's text field context menu (cut, copy, paste, select all), opened in Rack's scene.");
	focusField(field, "prev_field", &TF::prevField);
	focusField(field, "next_field", &TF::nextField);
	variant<ui::PasswordField, TF, TextFieldWidget<ui::PasswordField>>(mod, "PasswordField");

	// --- Menus ------------------------------------------------------------------------------
	variant<ui::MenuOverlay, OW>(mod, "MenuOverlay")
		.def_property("bg_color", [](py::object self) { return native<ui::MenuOverlay>(self).bgColor; },
			[](py::object self, NVGcolor c) { native<ui::MenuOverlay>(self).bgColor = c; });
	auto menu = variant<ui::Menu, OW>(mod, "Menu");
	menu.def_property_readonly("parent_menu", [](py::object self) { return wrapper(native<ui::Menu>(self).parentMenu); },
			"Rack's field; Rack 2.6.6 itself never sets it.")
		.def_property_readonly("child_menu", [](py::object self) { return wrapper(native<ui::Menu>(self).childMenu); })
		.def_property_readonly("active_entry", [](py::object self) { return wrapper(native<ui::Menu>(self).activeEntry); })
		.def_property("corner_flags", [](py::object self) { return (int) native<ui::Menu>(self).cornerFlags; },
			[](py::object self, int flags) { native<ui::Menu>(self).cornerFlags = (BNDcornerFlags) flags; },
			"blendish's BNDcornerFlags as an int (0: none, 15: all).")
		.def("set_child_menu", [](py::object self, py::object child) {
			auto& m = native<ui::Menu>(self);
			if (!m.parent)
				throw runtime::Ownership("a menu needs a parent (its MenuOverlay) before it can have a child menu");
			checkMutationOf(*m.parent);
			ui::Menu* next = nullptr;
			if (!child.is_none()) {
				auto s = state(child);
				auto* candidate = dynamic_cast<ui::Menu*>(&checked(s));
				if (!candidate)
					throw py::type_error("set_child_menu takes a rackpy.raw.ui.Menu or None");
				if (candidate->parent)
					throw runtime::Ownership("the child menu must not have a parent yet");
				next = handOver(child);
			}
			CallbackGuard guard;
			m.setChildMenu(next);  // Rack deletes the previous child menu and adds this one to the overlay
		}, py::arg("menu"), "Rack's Menu::setChildMenu: the menu goes to Rack, beside this one; the previous child menu is deleted.");
	variant<ui::MenuEntry, OW>(mod, "MenuEntry");
	using MI = ui::MenuItem;
	auto item = variant<MI, ui::MenuEntry, MenuItemWidget<MI>>(mod, "MenuItem");
	item.def_property("text", [](py::object self) { return native<MI>(self).text; },
			[](py::object self, std::string v) { native<MI>(self).text = std::move(v); })
		.def_property("right_text", [](py::object self) { return native<MI>(self).rightText; },
			[](py::object self, std::string v) { native<MI>(self).rightText = std::move(v); })
		.def_property("disabled", [](py::object self) { return native<MI>(self).disabled; },
			[](py::object self, bool v) { native<MI>(self).disabled = v; })
		.def("do_action", [](py::object self, bool consume) {
			bindings::refuseInAction("do_action");  // a menu item's action may push to Rack's history
			native<MI>(self).doAction(consume);
		},
			py::arg("consume") = true, "Rack's MenuItem::doAction: dispatch an ActionEvent and close the menu if consumed.")
		.def("create_child_menu", [](py::object self) -> py::object {
			ui::Menu* menu = native<MI>(self).MI::createChildMenu();
			if (!menu)
				return py::none();
			return wrapper(menu);
		}, "Rack's default: None. Override to return a new Menu; Rack then shows it beside this item.");
	using CD = ui::ColorDotMenuItem;
	variant<CD, MI, MenuItemWidget<CD>>(mod, "ColorDotMenuItem")
		.def_property("color", [](py::object self) { return native<CD>(self).color; },
			[](py::object self, NVGcolor c) { native<CD>(self).color = c; });
	variant<ui::MenuLabel, ui::MenuEntry>(mod, "MenuLabel")
		.def_property("text", [](py::object self) { return native<ui::MenuLabel>(self).text; },
			[](py::object self, std::string v) { native<ui::MenuLabel>(self).text = std::move(v); });
	variant<ui::MenuSeparator, ui::MenuEntry>(mod, "MenuSeparator");

	// rack::createMenu<TMenu>() (RP-26B): a menu at the mouse, in a MenuOverlay in Rack's scene.
	raw.def("create_menu", [mod](py::object menuType) {
		runtime::requireMainThread("opening a menu");
		auto* context = rack::contextGet();
		if (!context || !context->scene)
			throw runtime::WrongContext("menus need Rack's live UI context");
		py::object menuClass = menuType.is_none() ? mod.attr("Menu") : menuType;
		if (!PyType_Check(menuClass.ptr()) || !PyObject_IsSubclass(menuClass.ptr(), mod.attr("Menu").ptr()))
			throw py::type_error("create_menu takes a rackpy.raw.ui.Menu subclass");
		py::object menu = menuClass();
		native<ui::Menu>(menu).box.pos = context->scene->mousePos;
		py::object overlay = mod.attr("MenuOverlay")();
		auto overlayState = state(overlay);
		transferToHost(checked(overlayState), menu);  // the overlay owns the menu
		transferToHost(*context->scene, overlay);  // Rack's scene owns the overlay
		return menu;
	}, py::arg("menu_type") = py::none(),
		"Rack's createMenu<TMenu>(): a new menu at the mouse position, in a MenuOverlay added to Rack's scene. "
		"Rack closes it (deleting both) when the user clicks outside or picks an item.");

	// --- Theme and macros -------------------------------------------------------------------
	mod.def("set_theme", [](NVGcolor bg, NVGcolor fg) {
		runtime::requireMainThread("setting Rack's UI theme");
		ui::setTheme(bg, fg);
	}, py::arg("bg"), py::arg("fg"), "Rack's ui::setTheme: blendish's widget colours from a background and a foreground.");
	mod.def("refresh_theme", [] {
		runtime::requireMainThread("refreshing Rack's UI theme");
		ui::refreshTheme();
	}, "Rack's ui::refreshTheme: the theme for the current settings (light, dark or the user's).");
	mod.attr("CHECKMARK_STRING") = py::str(CHECKMARK_STRING);
	mod.attr("RIGHT_ARROW") = py::str(RIGHT_ARROW);
	mod.def("CHECKMARK", [](bool condition) { return std::string(CHECKMARK(condition)); }, py::arg("cond"),
		"The CHECKMARK(cond) macro: CHECKMARK_STRING if cond, else an empty string.");
}

}  // namespace widgets
}  // namespace rackpy
