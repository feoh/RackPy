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
/** RP-14B2b6: construct quantity_type() for slot `id` and disown it. A switch
or button slot needs a SwitchQuantity class. */
std::unique_ptr<rack::engine::ParamQuantity> makeTransferredQuantity(const ModuleRef& ref, pybind11::handle type,
	bool switchOnly, int id);
/** RP-14B2b7: a raw paramQuantities assignment of a Python-created quantity, or None (null). */
std::unique_ptr<rack::engine::ParamQuantity> takeQuantity(const ModuleRef& ref, pybind11::handle value);
/** Check a Python-created quantity's thread, slot ownership and liveness. */
void guardQuantity(rack::engine::ParamQuantity& q);

}  // namespace bindings
}  // namespace rackpy
