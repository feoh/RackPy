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
void bindModule(pybind11::module_ engine);

}  // namespace bindings
}  // namespace rackpy
