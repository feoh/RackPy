// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

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

}  // namespace widgets
}  // namespace rackpy
