// SPDX-License-Identifier: GPL-3.0-or-later
// RP-14B2b3/b4: owned SDK quantities with weak module links. No factory transfer.
#include "quantity.hpp"
#include "components.hpp"
#include "rackpy/runtime/python.hpp"
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
not installable in Module slots, and are never reached by an engine/audio thread.
A future ownership-transfer implementation must not reuse this UI-only guard. */
template <typename Base>
struct StandaloneQuantity : QuantityTrampoline<Base, StandaloneGuard> {
	std::weak_ptr<module::Construction> moduleState;
	std::shared_ptr<module::ModuleBackreference> moduleLink;
	StandaloneQuantity() { StandaloneGuard::check(); }
	~StandaloneQuantity() override {
		if (moduleLink)
			moduleLink->detach();
	}
	std::shared_ptr<module::Construction> owner() const {
		StandaloneGuard::check();
		if (!this->module)
			return {};
		auto held = moduleState.lock();
		if (!held || !held->object || held->object.get() != this->module)
			throw runtime::StaleHandle("standalone quantity has an untracked module pointer");
		return held;
	}
	void checkAccess(bool engine = false) const override {
		if (!owner() || !engine)
			return;
		module::Construction::index(this->paramId, this->module->params.size());
		auto context = runtime::binding().current();
		if (!context || rack::contextGet() != context->context || !context->context->engine)
			throw runtime::WrongContext("attached quantity value/display methods require Rack's live bound UI context");
		// Keep the GIL: these UI-only objects can invoke Python virtuals. No
		// other thread can close/reassign their main-thread-only owners here.
	}
	void setValue(float value) override {
		checkAccess(true);
		if (this->module && this->smoothEnabled)
			throw runtime::Ownership("smoothing targets require engine-owned modules; use set_immediate_value");
		Base::setValue(value);
	}
	float getValue() override { checkAccess(true); return Base::getValue(); }
	float immediate() { checkAccess(true); return this->getImmediateValue(); }
	void immediate(float value) { checkAccess(true); this->setImmediateValue(value); }
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
		checkAccess(true);
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

template <typename F>
decltype(auto) standalone(Q& q, F action) {
	if (auto* s = dynamic_cast<StandaloneQuantity<S>*>(&q))
		return action(*s);
	return action(dynamic_cast<StandaloneQuantity<Q>&>(q));
}

py::object quantityModule(Q& q) {
	return standalone(q, [](auto& alias) -> py::object {
		auto state = alias.owner();
		return state ? moduleObject(state) : py::none();
	});
}

void setQuantityModule(Q& q, py::object value) {
	StandaloneGuard::check();
	standalone(q, [&](auto& alias) {
		std::shared_ptr<module::Construction> state;
		std::shared_ptr<module::ModuleBackreference> link;
		if (!value.is_none()) {
			if (!py::isinstance<ModuleRef>(value))
				throw py::type_error("quantity.module must be a RackPy Module or None");
			state = value.cast<const ModuleRef&>().state;
			link = state->get().trackBackreference(&q.module);
		}
		// Prepare before detaching: invalid types, closed modules and native
		// allocation failures leave the old pointer and subscription intact.
		if (alias.moduleLink)
			alias.moduleLink->detach();
		alias.moduleLink = std::move(link);
		alias.moduleState = state;
		q.module = state ? state->object.get() : nullptr;
	});
}

}  // namespace

void bindStandaloneQuantities(py::module_ engine) {
	auto cls = py::classh<Q, rack::Quantity, StandaloneQuantity<Q>>(engine, "ParamQuantity",
		"Owned SDK ParamQuantity with a nullable weak module link. No class-factory transfer.");
	cls.def(py::init_alias<>())
		.def_property("module", &quantityModule, &setQuantityModule)
		.def("get_param", [](Q& q) -> py::object {
			return standalone(q, [&](auto& alias) -> py::object {
				auto state = alias.owner();
				auto* param = q.getParam();  // SDK returns null for invalid IDs.
				return param ? borrowParam(param, state->get().token) : py::none();
			});
		})
		.def("get_immediate_value", [](Q& q) { return standalone(q, [](auto& a) { return a.immediate(); }); })
		.def("set_immediate_value", [](Q& q, float value) { standalone(q, [value](auto& a) { a.immediate(value); }); }, py::arg("value"))
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
		"Owned SDK SwitchQuantity; mutable labels, weak module link, UI-safe overrides, no engine transfer.")
		.def(py::init_alias<>())
		.def_property("labels", [](py::object self) { return ownedSwitchLabels(self); },
			[](S& q, std::vector<std::string> labels) { StandaloneGuard::check(); q.labels = std::move(labels); });
}

}  // namespace bindings
}  // namespace rackpy
