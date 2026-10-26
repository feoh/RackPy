// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include <functional>
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
/** A module Model.create_module made for another plugin's model: Python owns it (RP-24D). */
pybind11::object ownForeignModule(rack::engine::Module* module);
/** CableWidget.set_cable (RP-28E): Python's Cable goes into Rack's engine for a widget to adopt at once, after
`check` accepts it (pointers set). The handle then finds it by ID. Main thread, GIL held. */
rack::engine::Cable* engineCableForWidget(pybind11::handle cable, const std::function<void(const rack::engine::Cable&)>& check);
/** Takes that cable out of the engine again and gives it back to its Python handle (the widget refused it). */
void undoEngineCableForWidget(pybind11::handle cable, rack::engine::Cable* raw);
/** A Python-owned Cable for a cable already taken out of Rack's engine. */
pybind11::object pythonOwnedCable(rack::engine::Cable* removed);
/** The ID-resolved Cable handle of an engine cable, or None outside a live context. */
pybind11::object engineCableHandle(int64_t id);
/** Whether a cable in Rack's engine touches this module (RP-24B). Main thread, GIL held. */
bool engineCablesTouch(rack::engine::Module& module);

}  // namespace bindings
}  // namespace rackpy
