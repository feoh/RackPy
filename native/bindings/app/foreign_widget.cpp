// SPDX-License-Identifier: GPL-3.0-or-later
// rackpy.raw.app.ForeignModuleWidget (RP-24D): a module widget that a model's
// own C++ code made (Model::createModuleWidget for another plugin's model, or
// RackPy's plain panel), which Python owns until Rack's rack takes it.
//
// Rack's widget owns its module: deleting the widget removes the module from
// the engine and deletes it, which takes Rack's engine lock. So a dropped
// widget is deleted on the main thread outside the lock (deleteOnMainThread).
// Rack indexes the rack's module widgets by their module, so a widget without
// one (a browser preview) never enters the rack.
#include <pybind11/pybind11.h>

#include <cmath>
#include <memory>
#include <unordered_set>

#include <rack.hpp>

#include "../engine/engine.hpp"
#include "../engine/engine_internal.hpp"
#include "../plugin.hpp"
#include "app.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace widgets {

namespace py = pybind11;
using rack::app::ModuleWidget;

namespace {

/** Modules whose widget Python owns: no second widget, cable or removal may touch them. */
std::unordered_set<const rack::engine::Module*> ownedModules;

struct Owned {
	std::unique_ptr<ModuleWidget, bindings::MainThreadDelete<ModuleWidget>> widget;
	const rack::engine::Module* module = nullptr;
	bool added = false;
	~Owned() {
		if (module)
			ownedModules.erase(module);
	}
};

struct ForeignWidget {
	std::shared_ptr<Owned> owned;
	ModuleWidget& get() const {
		runtime::requireMainThread("using a module widget");
		if (!owned->widget)
			throw runtime::StaleHandle(owned->added ? "Rack's rack has this module widget now (find it there)"
				: "this module widget was closed");
		return *owned->widget;
	}
};

}  // namespace

py::object ownModuleWidget(ModuleWidget* widget) {
	auto owned = std::make_shared<Owned>();
	owned->widget.reset(widget);
	if (widget->module) {
		owned->module = widget->module;
		ownedModules.insert(widget->module);
	}
	return py::cast(ForeignWidget{owned});
}

bool foreignWidgetOwns(const rack::engine::Module* module) {
	return ownedModules.count(module) != 0;
}

/** Gives Rack's rack a ForeignModuleWidget's widget through `add`, or returns false for other widgets. */
bool addForeignWidget(py::handle value, rack::app::RackWidget& rack, const std::function<void(ModuleWidget*)>& add) {
	if (!py::isinstance<ForeignWidget>(value))
		return false;
	auto& handle = value.cast<ForeignWidget&>();
	ModuleWidget& mw = handle.get();
	if (!mw.module)
		throw runtime::Ownership("Rack's rack indexes module widgets by their module: a preview cannot go in it");
	if (rack.getModule(mw.module->id))
		throw runtime::Ownership("this module already has its widget in the rack");
	add(&mw);  // Rack raises (a wrong size) before taking it
	handle.owned->widget.release();
	handle.owned->added = true;
	ownedModules.erase(handle.owned->module);
	handle.owned->module = nullptr;
	return true;
}

void bindForeignModuleWidget(py::module_ app) {
	py::class_<ForeignWidget>(app, "ForeignModuleWidget", py::is_final(),
		"A module widget a model's own code made (another plugin's, or RackPy's plain panel), which Python owns until "
		"Rack's rack takes it (HostRackWidget.add_module). Dropped, it is deleted with its module on the main thread.")
		.def_property_readonly("owned", [](const ForeignWidget& w) { return w.owned->widget != nullptr; },
			"True while Python owns it.")
		.def_property_readonly("added", [](const ForeignWidget& w) { return w.owned->added; },
			"True once Rack's rack took it.")
		.def_property_readonly("model", [](const ForeignWidget& w) -> py::object {
			auto* model = w.get().getModel();
			return model ? bindings::modelObject(model) : py::none();
		})
		.def_property_readonly("module", [](const ForeignWidget& w) { return bindings::engineModuleHandle(w.get().module); },
			"The module it owns (in Rack's engine), or None for a browser preview.")
		.def_property("box", [](const ForeignWidget& w) { return w.get().box; },
			[](const ForeignWidget& w, rack::math::Rect box) {
				if (!std::isfinite(box.pos.x) || !std::isfinite(box.pos.y) || !std::isfinite(box.size.x) ||
					!std::isfinite(box.size.y))
					throw std::invalid_argument("the box must be finite");
				w.get().box = box;
			}, "Rack's box: its position in the rack once added, and its size (Rack needs 3U high).")
		.def("close", [](const ForeignWidget& w) {
			runtime::requireMainThread("closing a module widget");
			if (!w.owned->widget)
				return;
			if (w.owned->module) {
				ownedModules.erase(w.owned->module);
				w.owned->module = nullptr;
			}
			w.owned->widget.reset();  // deletes its module too, outside Rack's engine lock
		}, "Deletes the widget, and with it its module (Rack's ModuleWidget does).");
}

}  // namespace widgets
}  // namespace rackpy
