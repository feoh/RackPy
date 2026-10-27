// SPDX-License-Identifier: GPL-3.0-or-later
// RP-14B2b3/b4: owned SDK quantities with weak module links. RP-14B2b6: the
// same objects transferred into module slots by the class factories.
#include "../support/overrides.hpp"
#include "quantity.hpp"
#include "components.hpp"
#include "metadata.hpp"
#include "rackpy/runtime/python.hpp"
#include "../support/hooks.hpp"
#include "../support/quantity.hpp"
#include "rackpy/runtime/json.hpp"

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
remain native: Python cannot override value/range methods (rackpy.raw refuses
such a subclass). Standalone objects are never reached by an engine/audio
thread, so their guard is UI-only. A slot-owned object can be: Rack then calls
without the GIL, and gets the SDK's native behaviour with no guard that could
throw into Rack (rackCaller). Calls from Python keep every check. */
template <typename Base>
struct StandaloneQuantity : QuantityTrampoline<Base, StandaloneGuard>, SlotTransfer<Base, StandaloneQuantity<Base>> {
	using Slot = SlotTransfer<Base, StandaloneQuantity<Base>>;
	using Trampoline = QuantityTrampoline<Base, StandaloneGuard>;
	StandaloneQuantity() { StandaloneGuard::check(); }
	void guard() const {
		if (this->rackCaller())
			return;
		StandaloneGuard::check();
		Slot::check("using a parameter quantity that a module owns");
	}
	std::shared_ptr<module::Construction> owner() const {
		guard();
		if (!this->module)
			return {};
		auto held = this->moduleState.lock();
		if (!held || held->current() != this->module)
			throw runtime::StaleHandle("parameter quantity has an untracked module pointer");
		return held;
	}
	void checkAccess(bool engine = false) const override {
		if (this->rackCaller() || !owner() || !engine)
			return;
		module::Construction::index(this->paramId, this->module->params.size());
		auto context = runtime::binding().current();
		if (!context || rack::contextGet() != context->context || !context->context->engine)
			throw runtime::WrongContext("attached quantity value/display methods require Rack's live bound UI context");
		// Keep the GIL: these UI-only objects can invoke Python virtuals. No
		// other thread can close/reassign their main-thread-only owners here.
	}
	void checkValueWrite() const override {
		checkAccess(true);
		if (!this->rackCaller() && this->module && this->smoothEnabled && !module::smoothingAllowed(this->module))
			throw runtime::Ownership("smoothing targets require engine-owned modules; use set_immediate_value");
	}
	void setValue(float value) override {
		checkValueWrite();
		Trampoline::setValue(value);
	}
	float getValue() override { checkAccess(true); return Trampoline::getValue(); }
	float immediate() { checkAccess(true); return this->getImmediateValue(); }
	void immediate(float value) { checkAccess(true); this->setImmediateValue(value); }
	float getMinValue() override { guard(); return Trampoline::getMinValue(); }
	float getMaxValue() override { guard(); return Trampoline::getMaxValue(); }
	float getDefaultValue() override { guard(); return Trampoline::getDefaultValue(); }
	std::string getLabel() override {
		guard();
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
		guard();
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_description", [this] { return Base::getDescription(); });
	}
	// JSON (RP-15A): Rack's paramsToJson calls toJson for every quantity (ui-shared).
	// A Python override's dict becomes the JSON; if it fails, Rack's answer is used.
	// Only a dict (RP-15C): paramsToJson adds the parameter's "id" to the object,
	// and paramsFromJson finds the value by it.
	json_t* toJson() override {
		checkAccess(true);
		json_t* result = nullptr;
		runPythonHook("ParamQuantity.to_json", [&] {
			if (auto f = rackpy::bindings::pythonOverride(static_cast<const Base*>(this), "to_json")) {
				py::object value = f();
				if (!PyDict_Check(value.ptr()))
					throw py::type_error(std::string("ParamQuantity.to_json must return a dict (Rack adds the parameter's "
						"\"id\" to it), not ") + Py_TYPE(value.ptr())->tp_name);
				result = runtime::toJson(value);
			}
		});
		return result ? result : Base::toJson();
	}
	void fromJson(json_t* rootJ) override {
		checkAccess(true);
		bool handled = false;
		runPythonHook("ParamQuantity.from_json", [&] {
			if (auto f = rackpy::bindings::pythonOverride(static_cast<const Base*>(this), "from_json")) {
				f(runtime::fromJson(rootJ));
				handled = true;
			}
		});
		if (!handled)
			Base::fromJson(rootJ);
	}
};

template <typename F>
decltype(auto) standalone(Q& q, F action) {
	if (auto* s = dynamic_cast<StandaloneQuantity<S>*>(&q))
		return action(*s);
	return action(dynamic_cast<StandaloneQuantity<Q>&>(q));
}

void guard(Q& q) {
	standalone(q, [](auto& alias) { alias.guard(); });
}

template <typename T>
void field(py::classh<Q, rack::Quantity, StandaloneQuantity<Q>>& cls, const char* name, T Q::*member) {
	cls.def_property(name, [member](Q& q) { guard(q); return q.*member; },
		[member](Q& q, T value) { guard(q); q.*member = std::move(value); });
}

py::object quantityModule(Q& q) {
	return standalone(q, [](auto& alias) -> py::object {
		auto state = alias.owner();
		return state ? moduleObject(state) : py::none();
	});
}

void setQuantityModule(Q& q, py::object value) {
	standalone(q, [&](auto& alias) {
		alias.guard();
		alias.requireIdentityEdit();
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
		q.module = state ? state->current() : nullptr;
	});
}

}  // namespace

void guardQuantity(Q& q) {
	guard(q);
}

/** Rack's engine thread reaches a module's slot quantities (Core MIDI-Map): a laboratory
quantity, whose value methods are Python, goes only into a laboratory module. */
void refuseLaboratoryQuantity(const ModuleRef& ref, Q& q) {
	auto* flag = dynamic_cast<LaboratoryQuantity*>(&q);
	if (flag && flag->laboratoryOverrides && !ref.get().laboratory)
		throw runtime::Laboratory("a laboratory parameter quantity goes only into a laboratory module's slot: Rack's "
			"engine can call an ordinary module's quantities on its own threads, where Python never runs");
}

std::unique_ptr<Q> takeQuantity(const ModuleRef& ref, py::handle value) {
	if (value.is_none())
		return nullptr;
	if (!py::isinstance<Q>(value))
		throw py::type_error("a module's quantity vector takes a ParamQuantity you created, or None");
	auto instance = py::reinterpret_borrow<py::object>(value);
	refuseLaboratoryQuantity(ref, instance.cast<Q&>());
	return standalone(instance.cast<Q&>(), [&](auto& alias) { return disownForSlot<Q>(instance, alias, ref.state, false); });
}

std::unique_ptr<Q> makeTransferredQuantity(const ModuleRef& ref, py::handle type, bool switchOnly, int id) {
	auto recheck = [&] {
		ref.state->requireConstruction();
		module::Construction::index(id, ref.get().paramQuantities.size());
	};
	py::object instance = switchOnly ? constructForSlot<S>(type, "quantity_type", "SwitchQuantity", recheck)
		: constructForSlot<Q>(type, "quantity_type", "ParamQuantity", recheck);
	refuseLaboratoryQuantity(ref, instance.cast<Q&>());
	return standalone(instance.cast<Q&>(), [&](auto& alias) { return disownForSlot<Q>(instance, alias, ref.state); });
}

void bindStandaloneQuantities(py::module_ engine) {
	auto cls = py::classh<Q, rack::Quantity, StandaloneQuantity<Q>>(engine, "ParamQuantity",
		"rack::engine::ParamQuantity with a nullable weak module link. A class factory may transfer it into a module slot.");
	cls.def(py::init_alias<>())
		.def_property("module", &quantityModule, &setQuantityModule)
		.def_property_readonly("alive", [](Q& q) { return standalone(q, [](auto& a) { return !a.retired; }); },
			"False once the module slot that owned this quantity replaced it or was deleted.")
		.def("get_param", [](Q& q) -> py::object {
			return standalone(q, [&](auto& alias) -> py::object {
				auto state = alias.owner();
				auto* param = q.getParam();  // SDK returns null for invalid IDs.
				return param ? borrowParam(param, state->get().paramsToken) : py::none();
			});
		})
		.def("get_immediate_value", [](Q& q) { return standalone(q, [](auto& a) { return a.immediate(); }); })
		.def("set_immediate_value", [](Q& q, float value) { standalone(q, [value](auto& a) { a.immediate(value); }); }, py::arg("value"))
		.def("get_description", [](Q& q) { return quantityTrampoline(q, false) ? q.Q::getDescription() : q.getDescription(); })
		// ParamQuantity's own overrides of Quantity's UI virtuals, bound on this
		// class so super() reaches this SDK declaration without redispatching. Native
		// subclasses still dispatch virtually.
		.def("get_label", [](Q& q) {
			if (!quantityTrampoline(q)) return q.getLabel();
			if (q.name.empty() && q.paramId == std::numeric_limits<int>::max())
				throw std::invalid_argument("parameter ID overflows Rack's default label");
			return q.Q::getLabel();
		})
		.def("get_unit", [](Q& q) { return quantityTrampoline(q, false) ? q.Q::getUnit() : q.getUnit(); })
		.def("get_display_precision", [](Q& q) { return quantityTrampoline(q, false) ? q.Q::getDisplayPrecision() : q.getDisplayPrecision(); })
		.def("get_display_value", [](Q& q) { return quantityTrampoline(q, true) ? q.Q::getDisplayValue() : q.getDisplayValue(); })
		.def("set_display_value", [](Q& q, float value) { quantityTrampoline(q, true) ? q.Q::setDisplayValue(value) : q.setDisplayValue(value); }, py::arg("display_value"))
		.def("get_display_value_string", [](Q& q) { return quantityTrampoline(q, true) ? q.Q::getDisplayValueString() : q.getDisplayValueString(); })
		.def("set_display_value_string", [](Q& q, std::string s) { quantityTrampoline(q, true) ? q.Q::setDisplayValueString(std::move(s)) : q.setDisplayValueString(std::move(s)); }, py::arg("s"))
		.def("reset", [](Q& q) { quantityTrampoline(q, true) ? q.Q::reset() : q.reset(); })
		// The mixed value and range methods: ParamQuantity's implementations
		// (value through Rack's engine), or a laboratory subclass's override (RP-22C).
		.def("get_value", [](Q& q) { return quantityTrampoline(q, true) ? q.Q::getValue() : q.getValue(); })
		.def("set_value", [](Q& q, float value) { quantityTrampoline(q, true, true) ? q.Q::setValue(value) : q.setValue(value); }, py::arg("value"))
		.def("get_min_value", [](Q& q) { return quantityTrampoline(q, false) ? q.Q::getMinValue() : q.getMinValue(); })
		.def("get_max_value", [](Q& q) { return quantityTrampoline(q, false) ? q.Q::getMaxValue() : q.getMaxValue(); })
		.def("get_default_value", [](Q& q) { return quantityTrampoline(q, false) ? q.Q::getDefaultValue() : q.getDefaultValue(); })
		.def("to_json", [](Q& q) {
			ErrorCapture capture;
			std::unique_ptr<json_t, decltype(&json_decref)> json(quantityTrampoline(q, true) ? q.Q::toJson() : q.toJson(), json_decref);
			capture.rethrow();
			return runtime::fromJson(json.get());
		}, "Rack's ParamQuantity::toJson, or a Python override's dict. Needs Rack's live bound UI context when attached.")
		.def("from_json", [](Q& q, py::handle data) {
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			ErrorCapture capture;
			if (quantityTrampoline(q, true)) q.Q::fromJson(json.get());
			else q.fromJson(json.get());
			capture.rethrow();
		}, py::arg("data"))
		.def("randomize", [](Q& q) { quantityTrampoline(q, true) ? q.Q::randomize() : q.randomize(); })
		.def("set_smooth_value", [](Q& q, float value) {
			guard(q);
			deprecated("set_smooth_value is deprecated in Rack; use set_value");
			quantityTrampoline(q, true, true) ? q.Q::setValue(value) : q.setValue(value);
		}, py::arg("value"))
		.def("get_smooth_value", [](Q& q) {
			guard(q);
			deprecated("get_smooth_value is deprecated in Rack; use get_value");
			return quantityTrampoline(q, true) ? q.Q::getValue() : q.getValue();
		});
	cls.def_property("param_id", [](Q& q) { guard(q); return q.paramId; }, [](Q& q, int id) {
		standalone(q, [](auto& alias) { alias.guard(); alias.requireIdentityEdit(); });
		q.paramId = id;
	});
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
		"rack::engine::SwitchQuantity: mutable labels, weak module link, UI-safe overrides; transferable to a module slot.")
		.def(py::init_alias<>())
		.def("get_display_value_string", [](S& q) {
			if (!quantityTrampoline(q, true)) return q.getDisplayValueString();
			double index = std::floor(q.getValue() - q.getMinValue());
			if (!std::isfinite(index) || index < std::numeric_limits<int>::min() || index > std::numeric_limits<int>::max())
				throw std::invalid_argument("switch label index cannot be represented as an integer");
			return q.S::getDisplayValueString();
		})
		.def("set_display_value_string", [](S& q, std::string s) { quantityTrampoline(q, true) ? q.S::setDisplayValueString(std::move(s)) : q.setDisplayValueString(std::move(s)); }, py::arg("s"))
		.def_property("labels", [](py::object self) {
			guard(self.cast<S&>());
			return ownedSwitchLabels(self);
		},
			[](S& q, std::vector<std::string> labels) { guard(q); q.labels = std::move(labels); });
}

}  // namespace bindings
}  // namespace rackpy
