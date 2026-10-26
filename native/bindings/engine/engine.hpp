// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <memory>

#include <rack.hpp>

#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace bindings {

/** rackpy.raw.engine.Engine and EngineModule (RP-24A). */
void bindEngine(pybind11::module_ engine);
/** Context::engine: the checked Engine handle, or None. Main thread, GIL. */
pybind11::object engineObject(const std::shared_ptr<runtime::ContextToken>& token);
/** A module of Rack's engine: its RackPy Module handle, an EngineModule for another plugin's, or None. */
pybind11::object engineModuleHandle(rack::engine::Module* module);

}  // namespace bindings
}  // namespace rackpy
