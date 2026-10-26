// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <functional>
#include <memory>

#include <rack.hpp>

namespace rackpy {
namespace plugin {
struct ModuleWidgetFactory;
}
}  // namespace rackpy

namespace rackpy {
namespace widgets {

/** rackpy.raw.app (RP-28A). Needs rackpy.raw.widget and rackpy.raw.ui bound first. */
void bindApp(pybind11::module_ raw);
/** A model's module widget factory from a Python callable (RP-28B): fn(module or None) -> ModuleWidget. */
std::unique_ptr<plugin::ModuleWidgetFactory> makeWidgetFactory(pybind11::object fn);
/** Model::createModuleWidget called from Python: the factory's widget, Python-owned. */
pybind11::object createModuleWidgetForPython(plugin::ModuleWidgetFactory& factory, rack::plugin::Model& model,
	pybind11::object module);
/** Whether a module widget owns this module: one in Rack's rack, or a Python one that took it. Main thread. */
bool moduleHasWidget(const rack::engine::Module* module);
/** Whether Rack may cable this port: Rack's own ports, or a Python port its module widget's factory made. */
bool portCableSafe(rack::app::PortWidget& port);
/** rackpy.raw.app.ForeignModuleWidget (RP-24D). */
void bindForeignModuleWidget(pybind11::module_ app);
/** A module widget a model's own code made: Python owns it until Rack's rack takes it. */
pybind11::object ownModuleWidget(rack::app::ModuleWidget* widget);
/** Whether a ForeignModuleWidget Python owns has this module. */
bool foreignWidgetOwns(const rack::engine::Module* module);
/** RackWidget.add_module for a ForeignModuleWidget (false for other widgets): `add` is Rack's addModule. */
bool addForeignWidget(pybind11::handle value, rack::app::RackWidget& rack,
	const std::function<void(rack::app::ModuleWidget*)>& add);

}  // namespace widgets
}  // namespace rackpy
