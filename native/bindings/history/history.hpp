// SPDX-License-Identifier: GPL-3.0-or-later
// rack::history -> rackpy.raw.history (RP-24C): undo actions and Rack's
// history State (Context.history).
#pragma once
#include <pybind11/pybind11.h>

#include <memory>

#include <rack.hpp>

#include "rackpy/runtime/context.hpp"

namespace rackpy {
namespace bindings {

/** rackpy.raw.history. Needs rackpy.raw.app and rackpy.raw.plugin bound first. */
void bindHistory(pybind11::module_ history);
/** Context::history: the checked State handle, or None. Main thread, GIL. */
pybind11::object historyObject(const std::shared_ptr<runtime::ContextToken>& token);
/** A ComplexAction Python owns, for Rack's calls that fill one (appendDisconnectActions). */
rack::history::ComplexAction& ownedComplexAction(pybind11::handle value);
/** A ComplexAction Rack made with new (getModuleDragAction): Python owns the result. */
pybind11::object adoptComplexAction(rack::history::ComplexAction* action);
/** Rack's State::push deletes the actions after the current one, which may include the one
running: so nothing that pushes to Rack's history runs while an action's undo or redo does. */
void refuseInAction(const char* what);

}  // namespace bindings
}  // namespace rackpy
