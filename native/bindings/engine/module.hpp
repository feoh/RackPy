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

pybind11::object moduleObject(const std::shared_ptr<module::Construction>& state);
/** A built-in slot's `module` field: its owner's handle or None. Writes are
constructor-only and take only the owner or None (RP-14B2b7). */
pybind11::object ownModuleField(rack::engine::Module* field, const std::shared_ptr<module::Construction>& state);
void setOwnModuleField(rack::engine::Module*& field, pybind11::handle value, const std::shared_ptr<module::Construction>& state);
std::shared_ptr<module::Construction> lockOwner(const std::weak_ptr<module::Construction>& state);
void bindModule(pybind11::module_ engine);

}  // namespace bindings
}  // namespace rackpy
