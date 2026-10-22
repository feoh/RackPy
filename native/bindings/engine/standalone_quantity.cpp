// SPDX-License-Identifier: GPL-3.0-or-later
// RP-14B2b3: unattached SDK quantities. No factory transfer or engine ownership.
#include "quantity.hpp"
#include "../support/quantity.hpp"

#include <pybind11/stl.h>

#include <limits>
#include <type_traits>

namespace rackpy {
namespace bindings {
namespace {

using Q = rack::engine::ParamQuantity;
using S = rack::engine::SwitchQuantity;

struct StandaloneGuard {
	static void check() { runtime::requireMainThread("using a standalone parameter quantity"); }
};

/** Always use the alias, including for the base Python types. Mixed virtuals
remain native: Python cannot override value/range methods. These objects are
not installable in Modules, and are never reached by an engine/audio thread.
A future ownership-transfer implementation must not reuse this UI-only guard. */
template <typename Base>
struct StandaloneQuantity : QuantityTrampoline<Base, StandaloneGuard> {
	StandaloneQuantity() { StandaloneGuard::check(); }
	void setValue(float value) override { StandaloneGuard::check(); Base::setValue(value); }
	float getValue() override { StandaloneGuard::check(); return Base::getValue(); }
	float getMinValue() override { StandaloneGuard::check(); return Base::getMinValue(); }
	float getMaxValue() override { StandaloneGuard::check(); return Base::getMaxValue(); }
	float getDefaultValue() override { StandaloneGuard::check(); return Base::getDefaultValue(); }
	std::string getLabel() override {
		StandaloneGuard::check();
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_label", [this] {
			// Guard only the SDK fallback, not a Python override that handles
			// raw metadata differently. Rack computes paramId + 1 as an int.
			if (this->name.empty() && this->paramId == std::numeric_limits<int>::max())
				throw std::invalid_argument("parameter ID overflows Rack's default label");
			return Base::getLabel();
		});
	}
	std::string getDisplayValueString() override {
		StandaloneGuard::check();
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_display_value_string", [this] {
			if constexpr (std::is_same<Base, S>::value) {
				// Rack casts floor(value - min) to int before checking the vector.
				double index = std::floor(this->getValue() - this->getMinValue());
				if (!std::isfinite(index) || index < std::numeric_limits<int>::min() || index > std::numeric_limits<int>::max())
					throw std::invalid_argument("switch label index cannot be represented as an integer");
			}
			return Base::getDisplayValueString();
		});
	}
	std::string getDescription() override {
		StandaloneGuard::check();
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_description", [this] { return Base::getDescription(); });
	}
};

template <typename T>
void field(py::classh<Q, rack::Quantity, StandaloneQuantity<Q>>& cls, const char* name, T Q::*member) {
	cls.def_property(name, [member](const Q& q) { StandaloneGuard::check(); return q.*member; },
		[member](Q& q, T value) { StandaloneGuard::check(); q.*member = std::move(value); });
}

}  // namespace

void bindStandaloneQuantities(py::module_ engine) {
	auto cls = py::classh<Q, rack::Quantity, StandaloneQuantity<Q>>(engine, "ParamQuantity",
		"Owned, unattached SDK ParamQuantity. No module attachment or class-factory transfer yet.");
	cls.def(py::init_alias<>())
		.def_property_readonly("module", [](const Q&) { StandaloneGuard::check(); return py::none(); })
		.def("get_param", [](Q& q) {
			StandaloneGuard::check();
			if (q.getParam())
				throw runtime::Ownership("standalone quantity unexpectedly attached");
			return py::none();
		},
			"Always None for these unattached quantities.")
		.def("get_immediate_value", [](Q& q) { StandaloneGuard::check(); return q.getImmediateValue(); })
		.def("set_immediate_value", [](Q& q, float value) { StandaloneGuard::check(); q.setImmediateValue(value); }, py::arg("value"))
		.def("get_description", &Q::getDescription)
		.def("set_smooth_value", [](Q& q, float value) {
			StandaloneGuard::check();
			deprecated("set_smooth_value is deprecated in Rack; use set_value");
			q.setValue(value);
		}, py::arg("value"))
		.def("get_smooth_value", [](Q& q) {
			StandaloneGuard::check();
			deprecated("get_smooth_value is deprecated in Rack; use get_value");
			return q.getValue();
		});
	field(cls, "param_id", &Q::paramId);
	field(cls, "min_value", &Q::minValue);
	field(cls, "max_value", &Q::maxValue);
	field(cls, "default_value", &Q::defaultValue);
	field(cls, "name", &Q::name);
	field(cls, "unit", &Q::unit);
	field(cls, "display_base", &Q::displayBase);
	field(cls, "display_multiplier", &Q::displayMultiplier);
	field(cls, "display_offset", &Q::displayOffset);
	field(cls, "display_precision", &Q::displayPrecision);
	field(cls, "description", &Q::description);
	field(cls, "reset_enabled", &Q::resetEnabled);
	field(cls, "randomize_enabled", &Q::randomizeEnabled);
	field(cls, "smooth_enabled", &Q::smoothEnabled);
	field(cls, "snap_enabled", &Q::snapEnabled);

	py::classh<S, Q, StandaloneQuantity<S>>(engine, "SwitchQuantity",
		"Owned, unattached SDK SwitchQuantity; mutable labels, UI-safe overrides, no engine transfer.")
		.def(py::init_alias<>())
		.def_property("labels", [](py::object self) { return ownedSwitchLabels(self); },
			[](S& q, std::vector<std::string> labels) { StandaloneGuard::check(); q.labels = std::move(labels); });
}

}  // namespace bindings
}  // namespace rackpy
