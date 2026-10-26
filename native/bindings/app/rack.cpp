// SPDX-License-Identifier: GPL-3.0-or-later
// Rack's scene, rack, rack scroll widget, cables and plugs as typed host views,
// and the LED display widgets (RP-28D). Rack makes and owns the scene and the
// rack (their constructors are PRIVATE), and the cable and plug widgets of the
// cables users plug; Python reaches their public API through checked views.
// Calls that take Rack's engine lock release the GIL (ADR 003, decision 2).
#include <rack.hpp>

#include <pybind11/stl.h>

#include <cmath>

#include "app_internal.hpp"
#include "rackpy/runtime/json.hpp"

namespace pybind11 {
#define RACKPY_RACK_ADDRESS(T) \
	template <> \
	struct polymorphic_type_hook<rack::app::T> { \
		static const void* get(const rack::app::T* ptr, const std::type_info*& type) { \
			type = &typeid(rack::app::T); \
			return ptr; \
		} \
	};
RACKPY_RACK_ADDRESS(LedDisplay)
RACKPY_RACK_ADDRESS(LedDisplaySeparator)
RACKPY_RACK_ADDRESS(LedDisplayChoice)
RACKPY_RACK_ADDRESS(LedDisplayTextField)
#undef RACKPY_RACK_ADDRESS
}  // namespace pybind11

namespace rackpy {
namespace widgets {
namespace appdetail {

namespace {

namespace app = rack::app;
using rack::engine::Port;

struct HostScene : ExternalWidget {};
struct HostRackScroll : ExternalWidget {};
struct HostRack : ExternalWidget {};
struct HostCable : ExternalWidget {};
struct HostPlug : ExternalWidget {};

template <class F>
auto unlocked(F f) {
	py::gil_scoped_release release;
	return f();
}

using Json = std::unique_ptr<json_t, decltype(&json_decref)>;
Json json(py::handle value) {
	return Json(runtime::toJson(value), json_decref);
}

template <class T>
py::list views(const std::vector<T*>& items) {
	py::list out;
	for (auto* item : items)
		out.append(wrapper(item));
	return out;
}

template <class T>
T* as(py::handle value, const char* what) {
	auto* w = dynamic_cast<T*>(argument(value));
	if (!w)
		throw py::type_error(std::string("expected ") + what);
	return w;
}

app::ModuleWidget& inRack(app::RackWidget& rack, py::handle value) {
	auto* mw = as<app::ModuleWidget>(value, "a rackpy.raw.app.ModuleWidget");
	if (!mw->module || rack.getModule(mw->module->id) != mw)
		throw runtime::Ownership("this module widget is not in Rack's rack");
	return *mw;
}

/** A patch's modules and cables must not already have widgets: Rack would make a second owner. */
void requireNewWidgets(app::RackWidget& rack, json_t* rootJ) {
	size_t i;
	json_t* item;
	json_array_foreach(json_object_get(rootJ, "modules"), i, item) {
		json_t* id = json_object_get(item, "id");
		if (id && rack.getModule(json_integer_value(id)))
			throw runtime::Ownership("module " + std::to_string(json_integer_value(id)) + " already has its widget in the rack");
	}
	json_array_foreach(json_object_get(rootJ, "cables"), i, item) {
		json_t* id = json_object_get(item, "id");
		if (id && rack.getCable(json_integer_value(id)))
			throw runtime::Ownership("cable " + std::to_string(json_integer_value(id)) + " already has its widget in the rack");
	}
}

void finite(rack::math::Vec v, const char* what) {
	if (!v.isFinite())
		throw std::invalid_argument(std::string(what) + " must be finite");
}

/** TextField::getTextPosition, which a Python subclass may override. */
template <class Base>
struct TextFieldT : Widget<Base> {
	int getTextPosition(rack::math::Vec mousePos) override {
		return this->template result<int>("get_text_position", [this, mousePos] { return Base::getTextPosition(mousePos); },
			mousePos);
	}
};

}  // namespace

void bindRack(py::module_ raw, py::module_ appModule) {
	py::module_ widget = raw.attr("widget").cast<py::module_>();

	py::class_<HostScene, ExternalWidget>(appModule, "HostScene", "Rack's Scene (Context.scene), a borrowed view.")
		.def_property_readonly("rack_scroll", [](const HostScene& h) { return wrapper(hostAs<app::Scene>(h).rackScroll); })
		.def_property_readonly("rack", [](const HostScene& h) { return wrapper(hostAs<app::Scene>(h).rack); })
		.def_property_readonly("menu_bar", [](const HostScene& h) { return wrapper(hostAs<app::Scene>(h).menuBar); })
		.def_property_readonly("browser", [](const HostScene& h) { return wrapper(hostAs<app::Scene>(h).browser); },
			"Rack's module browser (Browser.hpp is outside rack.hpp, so a plain HostWidget).")
		.def_property("mouse_pos", [](const HostScene& h) { return hostAs<app::Scene>(h).mousePos; },
			[](const HostScene& h, rack::math::Vec v) {
				finite(v, "mouse_pos");
				hostAs<app::Scene>(h).mousePos = v;
			}, "Rack's deprecated raw field (Rack sets it as the mouse moves); use get_mouse_pos().")
		.def("get_mouse_pos", [](const HostScene& h) { return hostAs<app::Scene>(h).getMousePos(); });
	registerHostView([](W* w) { return dynamic_cast<app::Scene*>(w) != nullptr; },
		[](const ExternalWidget& e) { return py::cast(HostScene{e}); });

	py::class_<HostRackScroll, ExternalWidget>(appModule, "HostRackScrollWidget", "Rack's RackScrollWidget, a borrowed view.")
		.def_property_readonly("zoom_widget", [](const HostRackScroll& h) { return wrapper(hostAs<app::RackScrollWidget>(h).zoomWidget); })
		.def_property_readonly("rack_widget", [](const HostRackScroll& h) { return wrapper(hostAs<app::RackScrollWidget>(h).rackWidget); })
		.def("reset", [](const HostRackScroll& h) { hostAs<app::RackScrollWidget>(h).reset(); }, "Rack's: the default zoom and offset.")
		.def("get_grid_offset", [](const HostRackScroll& h) { return hostAs<app::RackScrollWidget>(h).getGridOffset(); })
		.def("set_grid_offset", [](const HostRackScroll& h, rack::math::Vec v) {
			finite(v, "grid offset");
			hostAs<app::RackScrollWidget>(h).setGridOffset(v);
		}, py::arg("grid_offset"))
		.def("get_zoom", [](const HostRackScroll& h) { return hostAs<app::RackScrollWidget>(h).getZoom(); })
		.def("set_zoom", [](const HostRackScroll& h, float zoom) {
			if (!std::isfinite(zoom))
				throw std::invalid_argument("zoom must be finite");
			hostAs<app::RackScrollWidget>(h).setZoom(zoom);
		}, py::arg("zoom"), "Rack's: clamped to 1/4 to 4, about the view's centre.")
		.def("set_zoom", [](const HostRackScroll& h, float zoom, rack::math::Vec pivot) {
			if (!std::isfinite(zoom))
				throw std::invalid_argument("zoom must be finite");
			finite(pivot, "pivot");
			hostAs<app::RackScrollWidget>(h).setZoom(zoom, pivot);
		}, py::arg("zoom"), py::arg("pivot"))
		.def("zoom_to_modules", [](const HostRackScroll& h) { hostAs<app::RackScrollWidget>(h).zoomToModules(); })
		.def("zoom_to_bound", [](const HostRackScroll& h, rack::math::Rect bound) {
			// Rack divides by the bound's size and does not clamp the result.
			if (!bound.pos.isFinite() || !bound.size.isFinite() || bound.size.x <= 0.f || bound.size.y <= 0.f)
				throw std::invalid_argument("the bound must be finite, with a positive size");
			hostAs<app::RackScrollWidget>(h).zoomToBound(bound);
		}, py::arg("bound"));
	registerHostView([](W* w) { return dynamic_cast<app::RackScrollWidget*>(w) != nullptr; },
		[](const ExternalWidget& e) { return py::cast(HostRackScroll{e}); });

	using RW = app::RackWidget;
	py::class_<HostRack, ExternalWidget>(appModule, "HostRackWidget", "Rack's RackWidget: modules, cables and selection.")
		.def_property("touched_param", [](const HostRack& h) { return wrapper(hostAs<RW>(h).touchedParam); },
			[](const HostRack& h, py::object pw) {
				auto& rack = hostAs<RW>(h);
				if (pw.is_none()) {
					rack.touchedParam = nullptr;
					return;
				}
				auto* p = as<app::ParamWidget>(pw, "a rackpy.raw.app.ParamWidget or None");
				if (!p->module)
					throw runtime::Ownership("MIDI-Map reads the touched param's module: it must have one");
				rack.touchedParam = p;
			}, "Rack's deprecated raw field: get_touched_param/set_touched_param.")
		.def("get_touched_param", [](const HostRack& h) { return wrapper(hostAs<RW>(h).getTouchedParam()); })
		.def("set_touched_param", [](const HostRack& h, py::object pw) {
			auto& rack = hostAs<RW>(h);
			if (pw.is_none()) {
				rack.setTouchedParam(nullptr);
				return;
			}
			auto* p = as<app::ParamWidget>(pw, "a rackpy.raw.app.ParamWidget or None");
			if (!p->module)
				throw runtime::Ownership("MIDI-Map reads the touched param's module: it must have one");
			rack.setTouchedParam(p);
		}, py::arg("param_widget"), "The param Core's MIDI-Map learns next; it needs a module.")
		.def("get_module_container", [](const HostRack& h) { return wrapper(hostAs<RW>(h).getModuleContainer()); })
		.def("get_plug_container", [](const HostRack& h) { return wrapper(hostAs<RW>(h).getPlugContainer()); })
		.def("get_cable_container", [](const HostRack& h) { return wrapper(hostAs<RW>(h).getCableContainer()); })
		.def("get_mouse_pos", [](const HostRack& h) { return hostAs<RW>(h).getMousePos(); })
		.def("clear", [](const HostRack& h) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.clear(); });
		}, "Rack's: removes and deletes every module widget, with its module, and every cable (no undo step).")
		.def("merge_json", [](const HostRack& h, py::handle data) {
			auto& rack = hostAs<RW>(h);
			auto root = json(data);
			rack.mergeJson(root.get());
			return runtime::fromJson(root.get());
		}, py::arg("data"), "Rack's: patch JSON with the rack's module positions and cable details added; returns the result.")
		.def("from_json", [](const HostRack& h, py::handle data) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			auto root = json(data);
			requireNewWidgets(rack, root.get());
			unlocked([&] { rack.fromJson(root.get()); });
		}, py::arg("data"),
			"Rack's: widgets for the patch's modules and cables, which Rack's engine must already have (as when Rack "
			"opens a patch). Modules and cables that already have widgets are refused.")
		.def("paste_json_action", [](const HostRack& h, py::handle data) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			auto root = json(data);
			unlocked([&] { rack.pasteJsonAction(root.get()); });
		}, py::arg("data"), "Rack's paste of a selection's JSON, with an undo step.")
		.def("paste_module_json_action", [](const HostRack& h, py::handle data) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			auto module = json(data);
			unlocked([&] { rack.pasteModuleJsonAction(module.get()); });
		}, py::arg("data"), "Rack's paste of one module's JSON, with an undo step.")
		.def("paste_clipboard_action", [](const HostRack& h) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.pasteClipboardAction(); });
		})
		.def("add_module", [](const HostRack& h, py::object mwObject) {
			auto& rack = hostAs<RW>(h);
			auto* mw = as<app::ModuleWidget>(mwObject, "a rackpy.raw.app.ModuleWidget");
			if (!mw->model)
				throw runtime::Ownership("Rack needs the module widget's model");
			if (mw->module && rack.getModule(mw->module->id))
				throw runtime::Ownership("this module already has its widget in the rack");
			transferWith(*rack.getModuleContainer(), mwObject, [&rack](W* w) { rack.addModule(static_cast<app::ModuleWidget*>(w)); });
		}, py::arg("module_widget"),
			"Rack's addModule: a Python module widget (and its module) goes into the rack, which owns it from then on. "
			"Rack refuses one that is not 3U high and at least half an HP wide.")
		.def("add_module_at_mouse", [](const HostRack& h, py::object mwObject) {
			auto& rack = hostAs<RW>(h);
			auto* mw = as<app::ModuleWidget>(mwObject, "a rackpy.raw.app.ModuleWidget");
			if (!mw->model)
				throw runtime::Ownership("Rack needs the module widget's model");
			if (mw->module && rack.getModule(mw->module->id))
				throw runtime::Ownership("this module already has its widget in the rack");
			transferWith(*rack.getModuleContainer(), mwObject,
				[&rack](W* w) { rack.addModuleAtMouse(static_cast<app::ModuleWidget*>(w)); });
		}, py::arg("module_widget"))
		.def("remove_module", [](const HostRack& h, py::object mwObject) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			auto& mw = inRack(rack, mwObject);
			auto s = managedState(&mw);
			if (!s)
				throw runtime::Ownership("Rack made this module widget; delete it with its remove_action");
			unlocked([&] { rack.removeModule(&mw); });  // its cables go; the widget keeps its module
			s->reclaim();  // Python owns it again
		}, py::arg("module_widget"), "Rack's removeModule for a Python module widget: Python owns it again (and its module).")
		.def("get_module", [](const HostRack& h, int64_t id) { return wrapper(hostAs<RW>(h).getModule(id)); }, py::arg("module_id"))
		.def("get_modules", [](const HostRack& h) { return views(hostAs<RW>(h).getModules()); })
		.def("has_modules", [](const HostRack& h) { return hostAs<RW>(h).hasModules(); },
			"Rack's value, which in Rack 2.6.6 is True only when the rack is empty (it returns children.empty()).")
		.def("request_module_pos", [](const HostRack& h, py::object mw, rack::math::Vec pos) {
			finite(pos, "position");
			auto& rack = hostAs<RW>(h);
			return rack.requestModulePos(&inRack(rack, mw), pos);
		}, py::arg("module_widget"), py::arg("pos"), "Rack's: moves the widget to pos if it does not overlap another; True if it did.")
		.def("set_module_pos_nearest", [](const HostRack& h, py::object mw, rack::math::Vec pos) {
			finite(pos, "position");
			auto& rack = hostAs<RW>(h);
			rack.setModulePosNearest(&inRack(rack, mw), pos);
		}, py::arg("module_widget"), py::arg("pos"))
		.def("set_module_pos_force", [](const HostRack& h, py::object mw, rack::math::Vec pos) {
			finite(pos, "position");
			auto& rack = hostAs<RW>(h);
			rack.setModulePosForce(&inRack(rack, mw), pos);
		}, py::arg("module_widget"), py::arg("pos"))
		.def("set_module_pos_squeeze", [](const HostRack& h, py::object mw, rack::math::Vec pos) {
			finite(pos, "position");
			auto& rack = hostAs<RW>(h);
			rack.setModulePosSqueeze(&inRack(rack, mw), pos);
		}, py::arg("module_widget"), py::arg("pos"))
		.def("update_module_old_positions", [](const HostRack& h) { hostAs<RW>(h).updateModuleOldPositions(); })
		.def("update_selection_from_rect", [](const HostRack& h) { hostAs<RW>(h).updateSelectionFromRect(); })
		.def("select_all", [](const HostRack& h) { hostAs<RW>(h).selectAll(); })
		.def("deselect_all", [](const HostRack& h) { hostAs<RW>(h).deselectAll(); })
		.def("select", [](const HostRack& h, py::object mw, bool selected) {
			auto& rack = hostAs<RW>(h);
			rack.select(&inRack(rack, mw), selected);
		}, py::arg("module_widget"), py::arg("selected") = true)
		.def("has_selection", [](const HostRack& h) { return hostAs<RW>(h).hasSelection(); })
		.def("get_selected", [](const HostRack& h) {
			py::list out;
			for (auto* mw : hostAs<RW>(h).getSelected())
				out.append(wrapper(mw));
			return out;
		}, "A copy of Rack's selection.")
		.def("is_selected", [](const HostRack& h, py::object mw) {
			return hostAs<RW>(h).isSelected(as<app::ModuleWidget>(mw, "a rackpy.raw.app.ModuleWidget"));
		}, py::arg("module_widget"))
		.def("selection_to_json", [](const HostRack& h, bool cables) {
			auto& rack = hostAs<RW>(h);
			json_t* data = unlocked([&] { return rack.selectionToJson(cables); });
			Json held(data, json_decref);
			return runtime::fromJson(data);
		}, py::arg("cables") = true)
		.def("load_selection", [](const HostRack& h, const std::string& path) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.loadSelection(path); });
		}, py::arg("path"), "Rack's: pastes a .vcvs selection file; raises rackpy.raw.Exception if it cannot.")
		.def("load_selection_dialog", [](const HostRack& h) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.loadSelectionDialog(); });
		}, "Rack's: opens a file dialog and waits for the user.")
		.def("save_selection", [](const HostRack& h, const std::string& path) {
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.saveSelection(path); });
		}, py::arg("path"))
		.def("save_selection_dialog", [](const HostRack& h) {
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.saveSelectionDialog(); });
		}, "Rack's: opens a file dialog and waits for the user.")
		.def("copy_clipboard_selection", [](const HostRack& h) {
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.copyClipboardSelection(); });
		})
		.def("reset_selection_action", [](const HostRack& h) {
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.resetSelectionAction(); });
		})
		.def("randomize_selection_action", [](const HostRack& h) {
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.randomizeSelectionAction(); });
		})
		.def("disconnect_selection_action", [](const HostRack& h) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.disconnectSelectionAction(); });
		})
		.def("clone_selection_action", [](const HostRack& h, bool cloneCables) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.cloneSelectionAction(cloneCables); });
		}, py::arg("clone_cables") = true)
		.def("bypass_selection_action", [](const HostRack& h, bool bypassed) {
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.bypassSelectionAction(bypassed); });
		}, py::arg("bypassed"))
		.def("is_selection_bypassed", [](const HostRack& h) { return hostAs<RW>(h).isSelectionBypassed(); })
		.def("delete_selection_action", [](const HostRack& h) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.deleteSelectionAction(); });
		}, "Rack's Delete of the selected modules, with an undo step; their widgets' Python objects go stale.")
		.def("request_selection_pos", [](const HostRack& h, rack::math::Vec delta) {
			finite(delta, "delta");
			return hostAs<RW>(h).requestSelectionPos(delta);
		}, py::arg("delta"))
		.def("set_selection_pos_nearest", [](const HostRack& h, rack::math::Vec delta) {
			finite(delta, "delta");
			hostAs<RW>(h).setSelectionPosNearest(delta);
		}, py::arg("delta"))
		.def("append_selection_context_menu", [](const HostRack& h, py::object menu) {
			auto* m = menuArgument(menu);
			checkMutationOf(*m);
			CallbackGuard guard;
			hostAs<RW>(h).appendSelectionContextMenu(m);
		}, py::arg("menu"), "Rack's selection menu items, added to a rackpy.raw.ui.Menu.")
		.def("clear_cables", [](const HostRack& h) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.clearCables(); });
		}, "Rack's: removes every cable (no undo step).")
		.def("clear_cables_action", [](const HostRack& h) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			unlocked([&] { rack.clearCablesAction(); });
		}, "Rack's: removes every cable, with an undo step.")
		.def("clear_cables_on_port", [](const HostRack& h, py::object port) {
			checkMutation();
			auto& rack = hostAs<RW>(h);
			auto* p = as<app::PortWidget>(port, "a rackpy.raw.app.PortWidget");
			unlocked([&] { rack.clearCablesOnPort(p); });
		}, py::arg("port"))
		.def("get_incomplete_cable", [](const HostRack& h) {
			if (PyErr_WarnEx(PyExc_DeprecationWarning, "get_incomplete_cable mirrors deprecated getIncompleteCable; use "
				"get_incomplete_cables", 1) < 0)
				throw py::error_already_set();
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
			return wrapper(hostAs<RW>(h).getIncompleteCable());
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
		})
		.def("get_top_plug", [](const HostRack& h, py::object port) {
			return wrapper(hostAs<RW>(h).getTopPlug(as<app::PortWidget>(port, "a rackpy.raw.app.PortWidget")));
		}, py::arg("port"))
		.def("get_top_cable", [](const HostRack& h, py::object port) {
			return wrapper(hostAs<RW>(h).getTopCable(as<app::PortWidget>(port, "a rackpy.raw.app.PortWidget")));
		}, py::arg("port"))
		.def("get_cable", [](const HostRack& h, int64_t id) { return wrapper(hostAs<RW>(h).getCable(id)); }, py::arg("cable_id"))
		.def("get_cable", [](const HostRack& h, py::object out, py::object in) {
			return wrapper(hostAs<RW>(h).getCable(as<app::PortWidget>(out, "a PortWidget"), as<app::PortWidget>(in, "a PortWidget")));
		}, py::arg("output_port"), py::arg("input_port"))
		.def("get_cables", [](const HostRack& h) { return views(hostAs<RW>(h).getCables()); })
		.def("get_complete_cables", [](const HostRack& h) { return views(hostAs<RW>(h).getCompleteCables()); })
		.def("get_incomplete_cables", [](const HostRack& h) { return views(hostAs<RW>(h).getIncompleteCables()); })
		.def("get_cables_on_port", [](const HostRack& h, py::object port) {
			return views(hostAs<RW>(h).getCablesOnPort(as<app::PortWidget>(port, "a rackpy.raw.app.PortWidget")));
		}, py::arg("port"))
		.def("get_complete_cables_on_port", [](const HostRack& h, py::object port) {
			return views(hostAs<RW>(h).getCompleteCablesOnPort(as<app::PortWidget>(port, "a rackpy.raw.app.PortWidget")));
		}, py::arg("port"))
		.def("get_next_cable_color_id", [](const HostRack& h) { return hostAs<RW>(h).getNextCableColorId(); })
		.def("set_next_cable_color_id", [](const HostRack& h, int id) { hostAs<RW>(h).setNextCableColorId(id); }, py::arg("id"),
			"Rack's: wraps around its cable colour list.")
		.def("get_next_cable_color", [](const HostRack& h) { return hostAs<RW>(h).getNextCableColor(); });
	registerHostView([](W* w) { return dynamic_cast<RW*>(w) != nullptr; }, [](const ExternalWidget& e) { return py::cast(HostRack{e}); });

	using CW = app::CableWidget;
	py::class_<HostCable, ExternalWidget>(appModule, "HostCableWidget", "A cable widget Rack made, a borrowed view.")
		.def_property("color", [](const HostCable& h) { return hostAs<CW>(h).color; },
			[](const HostCable& h, NVGcolor c) { hostAs<CW>(h).color = c; })
		.def_property_readonly("input_plug", [](const HostCable& h) { return wrapper(hostAs<CW>(h).inputPlug); })
		.def_property_readonly("output_plug", [](const HostCable& h) { return wrapper(hostAs<CW>(h).outputPlug); })
		.def_property_readonly("input_port", [](const HostCable& h) { return wrapper(hostAs<CW>(h).inputPort); })
		.def_property_readonly("output_port", [](const HostCable& h) { return wrapper(hostAs<CW>(h).outputPort); })
		.def_property_readonly("hovered_input_port", [](const HostCable& h) { return wrapper(hostAs<CW>(h).hoveredInputPort); })
		.def_property_readonly("hovered_output_port", [](const HostCable& h) { return wrapper(hostAs<CW>(h).hoveredOutputPort); })
		.def("is_complete", [](const HostCable& h) { return hostAs<CW>(h).isComplete(); })
		.def("update_cable", [](const HostCable& h) {
			checkMutation();
			auto& cw = hostAs<CW>(h);
			unlocked([&] { cw.updateCable(); });
		}, "Rack's: replaces the engine cable with one between the widget's ports.")
		.def("get_plug", [](const HostCable& h, Port::Type type) { return wrapper(hostAs<CW>(h).getPlug(type)); }, py::arg("type"))
		.def("get_port", [](const HostCable& h, Port::Type type) { return wrapper(hostAs<CW>(h).getPort(type)); }, py::arg("type"))
		.def("get_hovered_port", [](const HostCable& h, Port::Type type) { return wrapper(hostAs<CW>(h).getHoveredPort(type)); },
			py::arg("type"))
		.def("get_input_pos", [](const HostCable& h) { return hostAs<CW>(h).getInputPos(); })
		.def("get_output_pos", [](const HostCable& h) { return hostAs<CW>(h).getOutputPos(); })
		.def("merge_json", [](const HostCable& h, py::handle data) {
			auto root = json(data);
			hostAs<CW>(h).mergeJson(root.get());
			return runtime::fromJson(root.get());
		}, py::arg("data"), "Rack's: the cable's JSON with its colour added; returns the result.")
		.def("from_json", [](const HostCable& h, py::handle data) {
			auto root = json(data);
			hostAs<CW>(h).fromJson(root.get());
		}, py::arg("data"), "Rack's: the cable's colour from its JSON.");
	registerHostView([](W* w) { return dynamic_cast<CW*>(w) != nullptr; }, [](const ExternalWidget& e) { return py::cast(HostCable{e}); });

	py::class_<HostPlug, ExternalWidget>(appModule, "HostPlugWidget", "A cable end Rack made, a borrowed view.")
		.def("get_cable", [](const HostPlug& h) { return wrapper(hostAs<app::PlugWidget>(h).getCable()); })
		.def("get_type", [](const HostPlug& h) { return hostAs<app::PlugWidget>(h).getType(); });
	registerHostView([](W* w) { return dynamic_cast<app::PlugWidget*>(w) != nullptr; },
		[](const ExternalWidget& e) { return py::cast(HostPlug{e}); });

	// LED displays: subclassable widgets, as Rack's own module widgets use them.
	variant<app::LedDisplay>(appModule, "LedDisplay");
	variant<app::LedDisplaySeparator>(appModule, "LedDisplaySeparator");
	using LC = app::LedDisplayChoice;
	variant<LC, rack::widget::OpaqueWidget>(appModule, "LedDisplayChoice")
		.def_property("text", [](py::object self) { return native<LC>(self).text; },
			[](py::object self, std::string v) { native<LC>(self).text = std::move(v); })
		.def_property("font_path", [](py::object self) { return native<LC>(self).fontPath; },
			[](py::object self, std::string v) { native<LC>(self).fontPath = std::move(v); },
			"A font file Rack loads each frame (Window.load_font); a missing one draws no text.")
		.def_property("text_offset", [](py::object self) { return native<LC>(self).textOffset; },
			[](py::object self, rack::math::Vec v) { native<LC>(self).textOffset = v; }, "A copy; assign a Vec to change it.")
		.def_property("color", [](py::object self) { return native<LC>(self).color; },
			[](py::object self, NVGcolor c) { native<LC>(self).color = c; })
		.def_property("bg_color", [](py::object self) { return native<LC>(self).bgColor; },
			[](py::object self, NVGcolor c) { native<LC>(self).bgColor = c; });
	using LT = app::LedDisplayTextField;
	variant<LT, rack::ui::TextField, TextFieldT<LT>>(appModule, "LedDisplayTextField")
		.def_property("font_path", [](py::object self) { return native<LT>(self).fontPath; },
			[](py::object self, std::string v) { native<LT>(self).fontPath = std::move(v); })
		.def_property("text_offset", [](py::object self) { return native<LT>(self).textOffset; },
			[](py::object self, rack::math::Vec v) { native<LT>(self).textOffset = v; }, "A copy; assign a Vec to change it.")
		.def_property("color", [](py::object self) { return native<LT>(self).color; },
			[](py::object self, NVGcolor c) { native<LT>(self).color = c; })
		.def_property("bg_color", [](py::object self) { return native<LT>(self).bgColor; },
			[](py::object self, NVGcolor c) { native<LT>(self).bgColor = c; })
		.def("get_text_position", [](py::object self, rack::math::Vec pos) { return native<LT>(self).LT::getTextPosition(pos); },
			py::arg("mouse_pos"));
	(void) widget;
}

}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
