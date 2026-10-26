// SPDX-License-Identifier: GPL-3.0-or-later
// rack::engine::Module::Expander -> rackpy.raw.engine (RP-23A); see expander.cpp.
#pragma once
#include <pybind11/pybind11.h>

#include "engine_internal.hpp"
#include "module.hpp"

namespace rackpy {
namespace bindings {

/** ExpanderMessage, ForeignMessage, Module.Expander and the Module accessors. */
void bindExpanders(pybind11::module_ engine, pybind11::class_<ModuleRef>& module);
/** The same accessors on EngineModule (another plugin's module). */
void bindEngineModuleExpanders(pybind11::class_<EngineModule>& cls);

}  // namespace bindings
}  // namespace rackpy
