// SPDX-License-Identifier: GPL-3.0-or-later
// rack::app controls -> rackpy.raw.app (RP-28A): Rack's param, port and light
// widgets and their SVG variants as subclassable Python classes, on RP-25's
// managed Widget machinery. Main thread only.
//
// Rack indexes a module's params, ports and lights with a control's IDs
// unchecked, keeps a raw module pointer in each control, and relies on every
// cabled port living inside its module's ModuleWidget. So a control takes only
// a RackPy module in Rack's engine, its IDs are checked against that module's
// fixed counts, the pointer is cleared when Rack deletes the module, and a
// Python port takes part in cables only inside its module's ModuleWidget.
#include <rack.hpp>

#include <pybind11/stl.h>

#include <unordered_map>
#include <unordered_set>

#include "../engine/module.hpp"
#include "../widget/variants.hpp"
#include "app.hpp"
#include "app_internal.hpp"
#include "../history/history.hpp"

namespace pybind11 {
#define RACKPY_APP_ADDRESS(T) \
	template <> \
	struct polymorphic_type_hook<rack::app::T> { \
		static const void* get(const rack::app::T* ptr, const std::type_info*& type) { \
			type = &typeid(rack::app::T); \
			return ptr; \
		} \
	};
RACKPY_APP_ADDRESS(ParamWidget)
RACKPY_APP_ADDRESS(Knob)
RACKPY_APP_ADDRESS(SliderKnob)
RACKPY_APP_ADDRESS(Switch)
RACKPY_APP_ADDRESS(PortWidget)
RACKPY_APP_ADDRESS(LightWidget)
RACKPY_APP_ADDRESS(MultiLightWidget)
RACKPY_APP_ADDRESS(ModuleLightWidget)
RACKPY_APP_ADDRESS(CircularShadow)
RACKPY_APP_ADDRESS(SvgKnob)
RACKPY_APP_ADDRESS(SvgSlider)
RACKPY_APP_ADDRESS(SvgSwitch)
RACKPY_APP_ADDRESS(SvgButton)
RACKPY_APP_ADDRESS(SvgPort)
RACKPY_APP_ADDRESS(ThemedSvgPort)
RACKPY_APP_ADDRESS(SvgScrew)
RACKPY_APP_ADDRESS(ThemedSvgScrew)
RACKPY_APP_ADDRESS(PanelBorder)
RACKPY_APP_ADDRESS(SvgPanel)
RACKPY_APP_ADDRESS(ThemedSvgPanel)
RACKPY_APP_ADDRESS(RailWidget)
#undef RACKPY_APP_ADDRESS
}  // namespace pybind11

namespace rackpy {
namespace widgets {

namespace {

namespace app = rack::app;
using appdetail::appendMenu;
using appdetail::guardPortEvents;
using appdetail::menuArgument;
using appdetail::paramMethods;
using appdetail::ParamWidgetT;
using appdetail::PortWidgetT;
using rack::engine::Port;
using SvgPtr = std::shared_ptr<rack::window::Svg>;
using OW = rack::widget::OpaqueWidget;
using TW = rack::widget::TransparentWidget;

// --- Module links ------------------------------------------------------------------

/** A control's module: the handle state (so its Python view stays reachable) and the
backreference that clears the control's pointer when Rack deletes the module. */
struct Link {
	std::shared_ptr<module::Construction> state;
	std::shared_ptr<module::ModuleBackreference> ref;
};
/** What RackPy tracks for a Python control: its module link, and the context menu
it last opened (Rack's menu items keep raw pointers to the control). */
struct Control {
	Link link;
	rack::WeakPtr<rack::ui::Menu> menu;
};
std::unordered_map<const State*, Control> controls;

/** Rack's touchedParam is a raw ParamWidget pointer that MIDI-Map dereferences. */
void releaseTouched(app::ParamWidget* widget) noexcept {
	auto token = runtime::binding().current();
	if (!token || !token->alive())
		return;
	auto* scene = token->context->scene;
	if (scene && scene->rack && scene->rack->touchedParam == widget)
		scene->rack->touchedParam = nullptr;
}

/** Closes a menu at once, as clicking outside it would on the next frame. */
void closeMenu(rack::ui::Menu* menu) noexcept {
	auto token = runtime::binding().current();
	if (!menu || !token || !token->alive())
		return;
	auto* overlay = menu->parent;
	auto* scene = token->context->scene;
	if (!overlay || !scene || overlay->parent != scene)
		return;
	try {
		CallbackGuard guard;
		scene->removeChild(overlay);  // finalizes it in Rack's event state
		delete overlay;
	}
	catch (...) {
		runtime::detail::noteRaised("closing a control's context menu", "Rack failed to delete the menu");
	}
}

/** The control's record, watched once: when the control dies its module link is detached,
its touched-param pointer released and its context menu closed. */
Control& control(const std::shared_ptr<State>& s, app::ParamWidget* touched) {
	auto inserted = controls.try_emplace(s.get());
	if (inserted.second) {
		try {
			s->onDestroyed.push_back([key = s.get(), touched] {
				auto it = controls.find(key);
				if (it != controls.end()) {
					if (it->second.link.ref)
						it->second.link.ref->detach();  // the module must not write into this freed widget
					rack::ui::Menu* menu = it->second.menu.get();
					controls.erase(it);
					closeMenu(menu);
				}
				if (touched)
					releaseTouched(touched);
			});
		}
		catch (...) {
			controls.erase(inserted.first);
			throw;
		}
	}
	return inserted.first->second;
}

/** Sets a control's module field to a RackPy module in Rack's engine, or None. */
void attach(py::handle self, rack::engine::Module*& field, py::handle value,
	const std::function<void(rack::engine::Module&)>& validate, app::ParamWidget* touched) {
	runtime::requireMainThread("setting a control's module");
	auto s = state(self);
	checked(s);
	if (value.is_none()) {
		auto it = controls.find(s.get());
		if (it != controls.end()) {
			if (it->second.link.ref)
				it->second.link.ref->detach();
			it->second.link = Link{};
		}
		field = nullptr;
		if (touched)
			releaseTouched(touched);
		return;
	}
	if (!py::isinstance<bindings::ModuleRef>(value))
		throw py::type_error("module must be a rackpy.raw.engine.Module in Rack's engine, or None");
	const auto& ref = value.cast<const bindings::ModuleRef&>();
	module::Module& m = ref.get();
	if (ref.state->constructing)
		throw runtime::Ownership("a module can be given to controls only after its constructor callback returns");
	if (!m.engineOwned.load(std::memory_order_acquire))
		throw runtime::Ownership("controls take a module in Rack's engine: Rack finds it by ID for history and cables");
	validate(m);
	Control& record = control(s, touched);
	auto backreference = m.trackBackreference(&field, touched ? std::function<void()>([touched] { releaseTouched(touched); })
		: std::function<void()>(), true);
	// Nothing below throws.
	if (record.link.ref)
		record.link.ref->detach();
	record.link = Link{ref.state, std::move(backreference)};
	field = &m;
}

py::object moduleOf(py::handle self, rack::engine::Module* field) {
	if (!field)
		return py::none();
	auto it = controls.find(state(self).get());
	if (it == controls.end() || !it->second.link.state || it->second.link.state->current() != field)
		throw runtime::StaleHandle("this control's module pointer is not tracked by RackPy");
	return bindings::moduleObject(it->second.link.state);
}

void requireId(int id, size_t count, const char* what) {
	if (id < 0 || static_cast<size_t>(id) >= count)
		throw std::invalid_argument(std::string(what) + " " + std::to_string(id) + " is out of range for this module (" +
			std::to_string(count) + ")");
}

/** Lights first..first+colors-1 (and lightInfos[first]) must exist; first == -1 is Rack's "none". */
void requireLights(rack::engine::Module& m, int first, size_t colors) {
	if (first == -1)
		return;
	if (first < 0 || static_cast<size_t>(first) >= m.lights.size() || static_cast<size_t>(first) + colors > m.lights.size())
		throw std::invalid_argument("lights " + std::to_string(first) + " to " + std::to_string(first + int(colors) - 1) +
			" are out of range for this module (" + std::to_string(m.lights.size()) + ")");
}

// --- Trampolines ---------------------------------------------------------------------

template <class Base>
struct LightWidgetT : Widget<Base> {
	void drawBackground(const W::DrawArgs& args) override {
		this->pyDraw("draw_background", args, [](auto& f, py::object v) { f(v); },
			[this, &args] { Base::drawBackground(args); });
	}
	void drawLight(const W::DrawArgs& args) override {
		this->pyDraw("draw_light", args, [](auto& f, py::object v) { f(v); }, [this, &args] { Base::drawLight(args); });
	}
	void drawHalo(const W::DrawArgs& args) override {
		this->pyDraw("draw_halo", args, [](auto& f, py::object v) { f(v); }, [this, &args] { Base::drawHalo(args); });
	}
};

template <bool Shadow, class T, class... Options>
void svgChildren(py::classh<T, Options...>& cls) {
	cls.def_property_readonly("fb", [](py::object self) { return wrapper(native<T>(self).fb); },
		"The FramebufferWidget Rack caches this control in (a HostFramebufferWidget).")
		.def_property_readonly("sw", [](py::object self) { return wrapper(native<T>(self).sw); },
			"The SvgWidget showing the SVG (a HostSvgWidget).");
	if constexpr (Shadow)
		cls.def_property_readonly("shadow", [](py::object self) { return wrapper(native<T>(self).shadow); },
			"The CircularShadow under the control (a HostCircularShadow).");
}

std::vector<SvgPtr> drawableFrames(std::vector<SvgPtr> frames) {
	for (auto& frame : frames) {
		if (!frame)
			throw std::invalid_argument("frames must be loaded Svgs, not None");
		drawableSvg(frame);
	}
	return frames;
}

void warnDeprecated(const char* message) {
	if (PyErr_WarnEx(PyExc_DeprecationWarning, message, 1) < 0)
		throw py::error_already_set();
}

struct HostShadow : ExternalWidget {};

}  // namespace

namespace appdetail {
namespace {
/** Ports a module widget created itself (RP-28B's Python module widget factories mark them). */
std::unordered_set<const State*> factoryPorts;
}  // namespace
bool cableSafe(rack::app::PortWidget& port) {
	if (!port.module)
		return false;
	if (!factoryPorts.count(state(static_cast<W*>(&port)).get()))
		return false;
	auto* mw = port.getAncestorOfType<rack::app::ModuleWidget>();
	return mw && mw->module == port.module;
}
}  // namespace appdetail

bool portCableSafe(rack::app::PortWidget& port) {
	return managedWidget(&port) ? appdetail::cableSafe(port) : true;
}

namespace appdetail {
void trackMenu(const std::shared_ptr<State>& s, rack::ui::Menu* menu, rack::app::ParamWidget* touched) {
	control(s, touched).menu.set(menu);
}
void markFactoryPort(const std::shared_ptr<State>& s) {
	if (!factoryPorts.insert(s.get()).second)
		return;
	s->onDestroyed.push_back([key = s.get()] { factoryPorts.erase(key); });
}
rack::ui::Menu* menuArgument(py::handle value) {
	auto* menu = dynamic_cast<rack::ui::Menu*>(argument(value));
	if (!menu)
		throw py::type_error("expected a rackpy.raw.ui.Menu");
	return menu;
}
}  // namespace appdetail

void bindApp(py::module_ raw) {
	py::module_ mod = raw.def_submodule("app", "rack::app: Rack's controls, ports, lights and panels (RP-28A).");
	using rack::app::RACK_GRID_WIDTH;
	mod.attr("RACK_GRID_WIDTH") = app::RACK_GRID_WIDTH;
	mod.attr("RACK_GRID_HEIGHT") = app::RACK_GRID_HEIGHT;
	mod.attr("RACK_GRID_SIZE") = app::RACK_GRID_SIZE;
	mod.attr("RACK_OFFSET") = app::RACK_OFFSET;

	// --- Params ---------------------------------------------------------------------------
	using PW = app::ParamWidget;
	auto param = variant<PW, OW, ParamWidgetT<PW>>(mod, "ParamWidget");
	paramMethods(param);
	param.def_property("module", [](py::object self) { return moduleOf(self, native<PW>(self).module); },
			[](py::object self, py::object value) {
				auto& w = native<PW>(self);
				attach(self, w.module, value, [&w](rack::engine::Module& m) { requireId(w.paramId, m.paramQuantities.size(), "param ID"); }, &w);
			}, "A RackPy module in Rack's engine, or None (as in a browser preview). param_id must be valid for it; Rack "
			"clears this when it deletes the module.")
		.def_property("param_id", [](py::object self) { return native<PW>(self).paramId; },
			[](py::object self, int id) {
				auto& w = native<PW>(self);
				if (w.module)
					requireId(id, w.module->paramQuantities.size(), "param ID");
				w.paramId = id;
			}, "Checked against the module, if one is set.")
		.def("get_param_quantity", [](py::object self) -> py::object {
			auto& w = native<PW>(self);
			if (!w.module)
				return py::none();
			return moduleOf(self, w.module).attr("param_quantities")[py::int_(w.paramId)];
		}, "The module's ParamQuantity for param_id, or None without a module.")
		.def("create_tooltip", [](py::object self) { checkMutation(); native<PW>(self).createTooltip(); })
		.def("destroy_tooltip", [](py::object self) { checkMutation(); native<PW>(self).destroyTooltip(); })
		.def("create_context_menu", [](py::object self) { checkMutation(); native<PW>(self).createContextMenu(); },
			"Rack's param context menu (value entry, reset, append_context_menu's items), opened in Rack's scene.")
		.def("reset_action", [](py::object self) {
			bindings::refuseInAction("reset_action");
			native<PW>(self).resetAction();
		},
			"Rack's reset to the default value, with an undo history action.");

	using K = app::Knob;
	auto knob = variant<K, PW, ParamWidgetT<K>>(mod, "Knob");
	paramMethods(knob);
	knob.def_property("horizontal", [](py::object self) { return native<K>(self).horizontal; },
			[](py::object self, bool v) { native<K>(self).horizontal = v; })
		.def_property("smooth", [](py::object self) { return native<K>(self).smooth; },
			[](py::object self, bool v) { native<K>(self).smooth = v; })
		.def_property("snap", [](py::object self) { return native<K>(self).snap; },
			[](py::object self, bool v) { native<K>(self).snap = v; })
		.def_property("speed", [](py::object self) { return native<K>(self).speed; },
			[](py::object self, float v) { native<K>(self).speed = v; })
		.def_property("force_linear", [](py::object self) { return native<K>(self).forceLinear; },
			[](py::object self, bool v) { native<K>(self).forceLinear = v; })
		.def_property("min_angle", [](py::object self) { return native<K>(self).minAngle; },
			[](py::object self, float v) { native<K>(self).minAngle = v; })
		.def_property("max_angle", [](py::object self) { return native<K>(self).maxAngle; },
			[](py::object self, float v) { native<K>(self).maxAngle = v; });
	auto sliderKnob = variant<app::SliderKnob, K, ParamWidgetT<app::SliderKnob>>(mod, "SliderKnob");
	paramMethods(sliderKnob);
	using SW = app::Switch;
	auto sw = variant<SW, PW, ParamWidgetT<SW>>(mod, "Switch");
	paramMethods(sw);
	sw.def_property("momentary", [](py::object self) { return native<SW>(self).momentary; },
		[](py::object self, bool v) { native<SW>(self).momentary = v; });

	// --- Ports ------------------------------------------------------------------------------
	using P = app::PortWidget;
	auto port = variant<P, OW, PortWidgetT<P>>(mod, "PortWidget");
	guardPortEvents(port);
	auto portCount = [](rack::engine::Module& m, Port::Type type) { return type == Port::INPUT ? m.inputs.size() : m.outputs.size(); };
	port.def_property("module", [](py::object self) { return moduleOf(self, native<P>(self).module); },
			[portCount](py::object self, py::object value) {
				auto& w = native<P>(self);
				attach(self, w.module, value, [&w, portCount](rack::engine::Module& m) { requireId(w.portId, portCount(m, w.type), "port ID"); },
					nullptr);
			}, "A RackPy module in Rack's engine, or None. type and port_id must be valid for it. Cables connect only while "
			"the port is inside this module's ModuleWidget.")
		.def_property("type", [](py::object self) { return native<P>(self).type; },
			[portCount](py::object self, Port::Type type) {
				auto& w = native<P>(self);
				if (type != Port::INPUT && type != Port::OUTPUT)
					throw std::invalid_argument("type must be Port.Type.INPUT or Port.Type.OUTPUT");
				if (w.module)
					requireId(w.portId, portCount(*w.module, type), "port ID");
				w.type = type;
			})
		.def_property("port_id", [](py::object self) { return native<P>(self).portId; },
			[portCount](py::object self, int id) {
				auto& w = native<P>(self);
				if (w.module)
					requireId(id, portCount(*w.module, w.type), "port ID");
				w.portId = id;
			}, "Checked against the module, if one is set.")
		.def("get_port", [](py::object self) -> py::object {
			auto& w = native<P>(self);
			if (!w.module)
				return py::none();
			return moduleOf(self, w.module).attr(w.type == Port::INPUT ? "inputs" : "outputs")[py::int_(w.portId)];
		})
		.def("get_port_info", [](py::object self) -> py::object {
			auto& w = native<P>(self);
			if (!w.module)
				return py::none();
			return moduleOf(self, w.module).attr(w.type == Port::INPUT ? "input_infos" : "output_infos")[py::int_(w.portId)];
		})
		.def("create_tooltip", [](py::object self) { checkMutation(); native<P>(self).createTooltip(); })
		.def("destroy_tooltip", [](py::object self) { checkMutation(); native<P>(self).destroyTooltip(); })
		.def("create_context_menu", [](py::object self) { checkMutation(); native<P>(self).createContextMenu(); })
		.def("append_context_menu", [](py::object self, py::object menu) { native<P>(self).P::appendContextMenu(menuArgument(menu)); },
			py::arg("menu"), "Rack's default adds nothing. Override to add items to the port's context menu.")
		.def("delete_top_cable_action", [](py::object self) {
			bindings::refuseInAction("delete_top_cable_action");
			checkMutation();
			native<P>(self).deleteTopCableAction();
		},
			"Rack's: deletes the top cable on this port, with an undo history action.");

	// --- Lights -------------------------------------------------------------------------------
	using L = app::LightWidget;
	auto light = variant<L, TW, LightWidgetT<L>>(mod, "LightWidget");
	auto lightMethods = [](auto& cls) {
		using C = typename std::remove_reference_t<decltype(cls)>::type;
		cls.def("draw_background", [](py::object self, py::object args) {
				auto owner = state(self);
				checkTraversal(owner);
				auto held = drawArgs(args);
				DrawGuard drawing(owner);
				CallbackGuard guard;
				native<C>(self).C::drawBackground(*static_cast<const W::DrawArgs*>(held->ptr));
			}, py::arg("args"))
			.def("draw_light", [](py::object self, py::object args) {
				auto owner = state(self);
				checkTraversal(owner);
				auto held = drawArgs(args);
				DrawGuard drawing(owner);
				CallbackGuard guard;
				native<C>(self).C::drawLight(*static_cast<const W::DrawArgs*>(held->ptr));
			}, py::arg("args"))
			.def("draw_halo", [](py::object self, py::object args) {
				auto owner = state(self);
				checkTraversal(owner);
				auto held = drawArgs(args);
				DrawGuard drawing(owner);
				CallbackGuard guard;
				native<C>(self).C::drawHalo(*static_cast<const W::DrawArgs*>(held->ptr));
			}, py::arg("args"));
	};
	lightMethods(light);
	light.def_property("bg_color", [](py::object self) { return native<L>(self).bgColor; },
			[](py::object self, NVGcolor c) { native<L>(self).bgColor = c; })
		.def_property("color", [](py::object self) { return native<L>(self).color; },
			[](py::object self, NVGcolor c) { native<L>(self).color = c; })
		.def_property("border_color", [](py::object self) { return native<L>(self).borderColor; },
			[](py::object self, NVGcolor c) { native<L>(self).borderColor = c; });
	using ML = app::MultiLightWidget;
	using MLW = app::ModuleLightWidget;
	auto multi = variant<ML, L, LightWidgetT<ML>>(mod, "MultiLightWidget");
	lightMethods(multi);
	auto checkColors = [](ML& w, size_t colors) {
		if (auto* moduleLight = dynamic_cast<MLW*>(&w); moduleLight && moduleLight->module)
			requireLights(*moduleLight->module, moduleLight->firstLightId, colors);
	};
	multi.def_property("base_colors", [](py::object self) { return native<ML>(self).baseColors; },
			[checkColors](py::object self, std::vector<NVGcolor> colors) {
				auto& w = native<ML>(self);
				checkColors(w, colors.size());
				w.baseColors = std::move(colors);
			}, "A copy; assign a list to change it. A ModuleLightWidget's module must have a light for each colour.")
		.def("get_num_colors", [](py::object self) { return native<ML>(self).getNumColors(); })
		.def("add_base_color", [checkColors](py::object self, NVGcolor c) {
			auto& w = native<ML>(self);
			checkColors(w, w.baseColors.size() + 1);
			w.addBaseColor(c);
		}, py::arg("base_color"))
		.def("set_brightnesses", [](py::object self, std::vector<float> brightnesses) {
			auto& w = native<ML>(self);
			if (brightnesses.size() != w.baseColors.size())
				throw std::invalid_argument("set_brightnesses needs one brightness per base colour (Rack reads them unchecked)");
			w.setBrightnesses(brightnesses);
		}, py::arg("brightnesses"), "Sets color to the base colours mixed by these weights.");
	auto moduleLight = variant<MLW, ML, LightWidgetT<MLW>>(mod, "ModuleLightWidget");
	lightMethods(moduleLight);
	moduleLight.def_property("module", [](py::object self) { return moduleOf(self, native<MLW>(self).module); },
			[](py::object self, py::object value) {
				auto& w = native<MLW>(self);
				attach(self, w.module, value, [&w](rack::engine::Module& m) { requireLights(m, w.firstLightId, w.baseColors.size()); },
					nullptr);
			}, "A RackPy module in Rack's engine, or None. It must have lights first_light_id onward, one per base colour.")
		.def_property("first_light_id", [](py::object self) { return native<MLW>(self).firstLightId; },
			[](py::object self, int id) {
				auto& w = native<MLW>(self);
				if (w.module)
					requireLights(*w.module, id, w.baseColors.size());
				w.firstLightId = id;
			}, "-1 for none. Checked against the module, if one is set.")
		.def("get_light", [](py::object self, int colorId) -> py::object {
			auto& w = native<MLW>(self);
			if (!w.module || w.firstLightId < 0)
				return py::none();
			if (colorId < 0 || colorId >= w.getNumColors())
				throw py::index_error("color_id must be from 0 to get_num_colors() - 1");
			return moduleOf(self, w.module).attr("lights")[py::int_(w.firstLightId + colorId)];
		}, py::arg("color_id"))
		.def("get_light_info", [](py::object self) -> py::object {
			auto& w = native<MLW>(self);
			if (!w.module || w.firstLightId < 0)
				return py::none();
			return moduleOf(self, w.module).attr("light_infos")[py::int_(w.firstLightId)];
		})
		.def("create_tooltip", [](py::object self) { checkMutation(); native<MLW>(self).createTooltip(); })
		.def("destroy_tooltip", [](py::object self) { checkMutation(); native<MLW>(self).destroyTooltip(); });

	// --- Shadows, SVG controls, screws, panels, rails -------------------------------------------------
	using CS = app::CircularShadow;
	variant<CS, TW>(mod, "CircularShadow")
		.def_property("blur_radius", [](py::object self) { return native<CS>(self).blurRadius; },
			[](py::object self, float v) { native<CS>(self).blurRadius = v; })
		.def_property("opacity", [](py::object self) { return native<CS>(self).opacity; },
			[](py::object self, float v) { native<CS>(self).opacity = v; });
	py::class_<HostShadow, ExternalWidget>(raw.attr("widget"), "HostCircularShadow", "A Rack-owned CircularShadow's public fields.")
		.def_property("blur_radius", [](const HostShadow& h) { return hostAs<CS>(h).blurRadius; },
			[](const HostShadow& h, float v) { hostAs<CS>(h).blurRadius = v; })
		.def_property("opacity", [](const HostShadow& h) { return hostAs<CS>(h).opacity; },
			[](const HostShadow& h, float v) { hostAs<CS>(h).opacity = v; });
	registerHostView([](W* w) { return dynamic_cast<CS*>(w) != nullptr; }, [](const ExternalWidget& e) { return py::cast(HostShadow{e}); });

	using SK = app::SvgKnob;
	auto svgKnob = variant<SK, K, ParamWidgetT<SK>>(mod, "SvgKnob");
	paramMethods(svgKnob);
	svgChildren<true>(svgKnob);
	svgKnob.def_property_readonly("tw", [](py::object self) { return wrapper(native<SK>(self).tw); },
			"The TransformWidget that rotates the SVG (a HostTransformWidget).")
		.def("set_svg", [](py::object self, SvgPtr svg) { native<SK>(self).setSvg(drawableSvg(std::move(svg))); }, py::arg("svg"))
		.def("set_svg_legacy", [](py::object self, SvgPtr svg) {
			warnDeprecated("set_svg_legacy mirrors deprecated SvgKnob::setSVG; use set_svg");
			native<SK>(self).setSvg(drawableSvg(std::move(svg)));
		}, py::arg("svg"));

	using SS = app::SvgSlider;
	auto slider = variant<SS, app::SliderKnob, ParamWidgetT<SS>>(mod, "SvgSlider");
	paramMethods(slider);
	slider.def_property_readonly("fb", [](py::object self) { return wrapper(native<SS>(self).fb); })
		.def_property_readonly("background", [](py::object self) { return wrapper(native<SS>(self).background); },
			"The SvgWidget of the slot (a HostSvgWidget).")
		.def_property_readonly("handle", [](py::object self) { return wrapper(native<SS>(self).handle); },
			"The SvgWidget of the handle (a HostSvgWidget).")
		.def_property("min_handle_pos", [](py::object self) { return native<SS>(self).minHandlePos; },
			[](py::object self, rack::math::Vec v) { native<SS>(self).minHandlePos = v; }, "A copy; assign a Vec to change it.")
		.def_property("max_handle_pos", [](py::object self) { return native<SS>(self).maxHandlePos; },
			[](py::object self, rack::math::Vec v) { native<SS>(self).maxHandlePos = v; }, "A copy; assign a Vec to change it.")
		.def("set_background_svg", [](py::object self, SvgPtr svg) { native<SS>(self).setBackgroundSvg(drawableSvg(std::move(svg))); },
			py::arg("svg"))
		.def("set_handle_svg", [](py::object self, SvgPtr svg) { native<SS>(self).setHandleSvg(drawableSvg(std::move(svg))); },
			py::arg("svg"))
		.def("set_handle_pos", [](py::object self, rack::math::Vec lo, rack::math::Vec hi) { native<SS>(self).setHandlePos(lo, hi); },
			py::arg("min_handle_pos"), py::arg("max_handle_pos"))
		.def("set_handle_pos_centered", [](py::object self, rack::math::Vec lo, rack::math::Vec hi) {
			native<SS>(self).setHandlePosCentered(lo, hi);
		}, py::arg("min_handle_pos_centered"), py::arg("max_handle_pos_centered"))
		.def("set_background_svg_legacy", [](py::object self, SvgPtr svg) {
			warnDeprecated("set_background_svg_legacy mirrors deprecated setBackgroundSVG; use set_background_svg");
			native<SS>(self).setBackgroundSvg(drawableSvg(std::move(svg)));
		}, py::arg("svg"))
		.def("set_handle_svg_legacy", [](py::object self, SvgPtr svg) {
			warnDeprecated("set_handle_svg_legacy mirrors deprecated setHandleSVG; use set_handle_svg");
			native<SS>(self).setHandleSvg(drawableSvg(std::move(svg)));
		}, py::arg("svg"))
		.def("set_svgs", [](py::object self, SvgPtr background, SvgPtr handle) {
			warnDeprecated("set_svgs mirrors deprecated setSVGs; use set_background_svg and set_handle_svg");
			native<SS>(self).setBackgroundSvg(drawableSvg(std::move(background)));
			native<SS>(self).setHandleSvg(drawableSvg(std::move(handle)));
		}, py::arg("background_svg"), py::arg("handle_svg"));

	using SWI = app::SvgSwitch;
	auto svgSwitch = variant<SWI, SW, ParamWidgetT<SWI>>(mod, "SvgSwitch");
	paramMethods(svgSwitch);
	svgChildren<true>(svgSwitch);
	svgSwitch.def_property("frames", [](py::object self) { return native<SWI>(self).frames; },
			[](py::object self, std::vector<SvgPtr> frames) { native<SWI>(self).frames = drawableFrames(std::move(frames)); },
			"A copy of the loaded SVGs, one per value; assign a list to change it.")
		.def_property("latch", [](py::object self) { return native<SWI>(self).latch; },
			[](py::object self, bool v) { native<SWI>(self).latch = v; })
		.def("add_frame", [](py::object self, SvgPtr svg) {
			if (!svg)
				throw std::invalid_argument("add_frame takes a loaded Svg, not None");
			native<SWI>(self).addFrame(drawableSvg(std::move(svg)));
		}, py::arg("svg"));

	using SB = app::SvgButton;
	auto svgButton = variant<SB, OW>(mod, "SvgButton");
	svgChildren<true>(svgButton);
	svgButton.def_property("frames", [](py::object self) { return native<SB>(self).frames; },
			[](py::object self, std::vector<SvgPtr> frames) { native<SB>(self).frames = drawableFrames(std::move(frames)); },
			"A copy of the loaded SVGs (released, pressed); assign a list to change it.")
		.def("add_frame", [](py::object self, SvgPtr svg) {
			if (!svg)
				throw std::invalid_argument("add_frame takes a loaded Svg, not None");
			native<SB>(self).addFrame(drawableSvg(std::move(svg)));
		}, py::arg("svg"));

	using SP = app::SvgPort;
	auto svgPort = variant<SP, P, PortWidgetT<SP>>(mod, "SvgPort");
	guardPortEvents(svgPort);
	svgChildren<true>(svgPort);
	svgPort.def("set_svg", [](py::object self, SvgPtr svg) { native<SP>(self).setSvg(drawableSvg(std::move(svg))); }, py::arg("svg"))
		.def("set_svg_legacy", [](py::object self, SvgPtr svg) {
			warnDeprecated("set_svg_legacy mirrors deprecated SvgPort::setSVG; use set_svg");
			native<SP>(self).setSvg(drawableSvg(std::move(svg)));
		}, py::arg("svg"));
	using TSP = app::ThemedSvgPort;
	auto themedPort = variant<TSP, SP, PortWidgetT<TSP>>(mod, "ThemedSvgPort");
	guardPortEvents(themedPort);
	themedPort.def_property("light_svg", [](py::object self) { return native<TSP>(self).lightSvg; },
			[](py::object self, SvgPtr svg) { native<TSP>(self).lightSvg = drawableSvg(std::move(svg)); })
		.def_property("dark_svg", [](py::object self) { return native<TSP>(self).darkSvg; },
			[](py::object self, SvgPtr svg) { native<TSP>(self).darkSvg = drawableSvg(std::move(svg)); })
		.def("set_svg", [](py::object self, SvgPtr lightSvg, SvgPtr darkSvg) {
			native<TSP>(self).setSvg(drawableSvg(std::move(lightSvg)), drawableSvg(std::move(darkSvg)));
		}, py::arg("light_svg"), py::arg("dark_svg"), "Shows dark_svg while Rack prefers dark panels, else light_svg.");

	using SCR = app::SvgScrew;
	auto screw = variant<SCR>(mod, "SvgScrew");
	svgChildren<false>(screw);
	screw.def("set_svg", [](py::object self, SvgPtr svg) { native<SCR>(self).setSvg(drawableSvg(std::move(svg))); }, py::arg("svg"));
	using TSCR = app::ThemedSvgScrew;
	variant<TSCR, SCR>(mod, "ThemedSvgScrew")
		.def_property("light_svg", [](py::object self) { return native<TSCR>(self).lightSvg; },
			[](py::object self, SvgPtr svg) { native<TSCR>(self).lightSvg = drawableSvg(std::move(svg)); })
		.def_property("dark_svg", [](py::object self) { return native<TSCR>(self).darkSvg; },
			[](py::object self, SvgPtr svg) { native<TSCR>(self).darkSvg = drawableSvg(std::move(svg)); })
		.def("set_svg", [](py::object self, SvgPtr lightSvg, SvgPtr darkSvg) {
			native<TSCR>(self).setSvg(drawableSvg(std::move(lightSvg)), drawableSvg(std::move(darkSvg)));
		}, py::arg("light_svg"), py::arg("dark_svg"));

	variant<app::PanelBorder, TW>(mod, "PanelBorder");
	using PNL = app::SvgPanel;
	auto panel = variant<PNL>(mod, "SvgPanel");
	panel.def_property_readonly("fb", [](py::object self) { return wrapper(native<PNL>(self).fb); })
		.def_property_readonly("sw", [](py::object self) { return wrapper(native<PNL>(self).sw); })
		.def_property_readonly("panel_border", [](py::object self) { return wrapper(native<PNL>(self).panelBorder); })
		.def_property("svg", [](py::object self) { return native<PNL>(self).svg; },
			[](py::object self, SvgPtr svg) { native<PNL>(self).svg = drawableSvg(std::move(svg)); },
			"The raw field; set_background() also shows it and sizes the panel.")
		.def("set_background", [](py::object self, SvgPtr svg) { native<PNL>(self).setBackground(drawableSvg(std::move(svg))); },
			py::arg("svg"));
	using TPNL = app::ThemedSvgPanel;
	variant<TPNL, PNL>(mod, "ThemedSvgPanel")
		.def_property("light_svg", [](py::object self) { return native<TPNL>(self).lightSvg; },
			[](py::object self, SvgPtr svg) { native<TPNL>(self).lightSvg = drawableSvg(std::move(svg)); })
		.def_property("dark_svg", [](py::object self) { return native<TPNL>(self).darkSvg; },
			[](py::object self, SvgPtr svg) { native<TPNL>(self).darkSvg = drawableSvg(std::move(svg)); })
		.def("set_background", [](py::object self, SvgPtr lightSvg, SvgPtr darkSvg) {
			native<TPNL>(self).setBackground(drawableSvg(std::move(lightSvg)), drawableSvg(std::move(darkSvg)));
		}, py::arg("light_svg"), py::arg("dark_svg"), "Shows dark_svg while Rack prefers dark panels, else light_svg.");
	variant<app::RailWidget, TW>(mod, "RailWidget");

	appdetail::bindModuleWidget(mod);
	bindForeignModuleWidget(mod);
	appdetail::bindCableWidget(mod);
	appdetail::bindRack(raw, mod);
	appdetail::bindComponentLibrary(raw);
	appdetail::bindIoDisplays(raw, mod);

	// Rack's deprecated SVG* typedefs: the same classes, with a warning when used.
	static const std::pair<const char*, const char*> aliases[] = {{"SVGKnob", "SvgKnob"}, {"SVGSlider", "SvgSlider"},
		{"SVGSwitch", "SvgSwitch"}, {"SVGButton", "SvgButton"}, {"SVGPort", "SvgPort"}, {"SVGScrew", "SvgScrew"},
		{"SVGPanel", "SvgPanel"}};
	py::list names;
	for (auto item : mod.attr("__dict__").cast<py::dict>())
		if (item.first.cast<std::string>().rfind("_", 0) != 0)
			names.append(item.first);
	for (const auto& alias : aliases)
		names.append(alias.first);
	mod.attr("__all__") = names;
	mod.attr("__getattr__") = py::cpp_function([](const std::string& name) -> py::object {
		for (const auto& alias : aliases)
			if (name == alias.first) {
				warnDeprecated((std::string("app.") + alias.first + " mirrors a deprecated typedef; use app." + alias.second).c_str());
				return py::module_::import("_rackpy_host").attr("raw").attr("app").attr(alias.second);
			}
		throw py::attribute_error("module 'rackpy.raw.app' has no attribute '" + name + "'");
	});
}

}  // namespace widgets
}  // namespace rackpy
