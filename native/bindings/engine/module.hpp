// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <pybind11/pybind11.h>

#include "../../module/module.hpp"

namespace rackpy {
namespace bindings {

struct ModuleRef {
	std::shared_ptr<module::Construction> state;
	module::Module& get() const { return state->get(); }
};

/** How a component, metadata or quantity view finds its module's handle
state. A Python-owned module dies with its last handle, but one Rack's engine
owns outlives its handles, so a view found through a temporary handle finds
the module again through the module's own token while Rack keeps it. Main
thread. */
struct OwnerState {
	std::weak_ptr<module::Construction> state;
	module::Module* module = nullptr;
	std::shared_ptr<runtime::ObjectToken> moduleToken;
	OwnerState() = default;
	OwnerState(const std::shared_ptr<module::Construction>& held) : state(held) {
		if (held && (module = held->current()))
			moduleToken = module->token;
	}
	/** The module's handle state, or null if the module is gone. */
	std::shared_ptr<module::Construction> lock() const {
		if (auto held = state.lock())
			return held;
		if (module && moduleToken && moduleToken->alive() && module->engineOwned.load(std::memory_order_acquire))
			return module::Construction::handleState(*module);
		return nullptr;
	}
};

pybind11::object moduleObject(const std::shared_ptr<module::Construction>& state);
/** Module.create's transaction for a given construction: a laboratory module's (RP-22). Main thread. */
pybind11::object createWith(std::shared_ptr<module::Construction> state, pybind11::object configure);
/** A built-in slot's `module` field: its owner's handle or None. Writes are
constructor-only and take only the owner or None (RP-14B2b7). */
pybind11::object ownModuleField(rack::engine::Module* field, const std::shared_ptr<module::Construction>& state);
void setOwnModuleField(rack::engine::Module*& field, pybind11::handle value, const std::shared_ptr<module::Construction>& state);
std::shared_ptr<module::Construction> lockOwner(const std::weak_ptr<module::Construction>& state);
std::shared_ptr<module::Construction> lockOwner(const OwnerState& state);
void bindModule(pybind11::module_ engine);
/** Rack's Model::createModule for a Python factory (RP-16A): Module.create(configure),
then the sealed module, its model set, goes to Rack, which adds it to its engine.
Main thread, with the GIL; throws what the constructor raises. */
rack::engine::Module* createForRack(pybind11::object configure, rack::plugin::Model& model);
/** Model::createModule called from Python (RP-28B): the same module, with its model set, but
Python-owned (a Module handle), since nobody hands it to Rack's engine. */
pybind11::object createOwned(pybind11::object configure, rack::plugin::Model& model);
/** The handle of a RackPy module Rack's engine owns, or None for anything else. Main thread, GIL. */
pybind11::object engineModuleObject(rack::engine::Module* module);

}  // namespace bindings
}  // namespace rackpy
