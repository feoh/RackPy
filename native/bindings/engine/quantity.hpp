// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "module.hpp"

namespace rackpy {
namespace bindings {

struct QuantityView {
	rack::engine::ParamQuantity* ptr;
	std::shared_ptr<runtime::ObjectToken> token;
	std::weak_ptr<module::Construction> state;
	rack::engine::ParamQuantity& get() const;
	pybind11::object owner() const;
};
struct SwitchQuantityView : QuantityView {};

pybind11::object quantityView(const ModuleRef& ref, int id);
void bindQuantityViews(pybind11::module_ engine);
void bindStandaloneQuantities(pybind11::module_ engine);
pybind11::object ownedSwitchLabels(pybind11::object owner);

}  // namespace bindings
}  // namespace rackpy
