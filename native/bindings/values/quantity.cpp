// SPDX-License-Identifier: GPL-3.0-or-later
// rack::Quantity (Quantity.hpp) -> rackpy.raw.Quantity, and RackPy's native
// ValueQuantity -> rackpy.types.ValueQuantity.
//
// Callback strategies (bin/callback-matrix, ADR 003):
// - ui: getDisplayValue, setDisplayValue, getDisplayPrecision,
//   getDisplayValueString, setDisplayValueString, getLabel, getUnit, getString.
//   Python subclasses may override them.
// - ui-exclusive: reset, randomize. Python may override them; Rack can call
//   them with audio stopped, so time spent there is measured against ADR 004's
//   1 ms budget.
// - mixed: getValue, setValue, getMinValue, getMaxValue, getDefaultValue. Rack
//   also calls them on the engine thread (Core MIDI-Map), so they have no
//   Python path: the trampoline does not override them, and rackpy.raw refuses
//   a subclass that defines them. ValueQuantity serves them natively instead.
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include "../support/overrides.hpp"
#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"
#include "values.hpp"

namespace rackpy {
namespace values {

namespace rt = rackpy::runtime;
using bindings::overrideOr;
using bindings::overrideOrVoid;

namespace {

/** Time a ui-exclusive override when Rack runs it with the engine locked (ADR 004: 1 ms budget). */
struct ExclusiveBudget {
	const char* name;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	explicit ExclusiveBudget(const char* name) : name(name) {}
	~ExclusiveBudget() {
		if (!rt::insideExclusive())
			return;
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		if (ms > 1.0)
			rt::diagnose(rt::DiagnosticCode::SlowExclusiveCallback, name);
	}
};

/** The Python-overridable virtuals, shared by both trampolines. Base is rack::Quantity or ValueQuantity. */
template <typename Base>
struct QuantityTrampoline : Base, py::trampoline_self_life_support {
	using Base::Base;
	float getDisplayValue() override {
		return overrideOr<float>(static_cast<const Base*>(this), "get_display_value", [this] { return Base::getDisplayValue(); });
	}
	void setDisplayValue(float v) override {
		overrideOrVoid(static_cast<const Base*>(this), "set_display_value", [this, v] { Base::setDisplayValue(v); }, v);
	}
	int getDisplayPrecision() override {
		return overrideOr<int>(static_cast<const Base*>(this), "get_display_precision", [this] { return Base::getDisplayPrecision(); });
	}
	std::string getDisplayValueString() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_display_value_string", [this] { return Base::getDisplayValueString(); });
	}
	void setDisplayValueString(std::string s) override {
		overrideOrVoid(static_cast<const Base*>(this), "set_display_value_string", [this, s] { Base::setDisplayValueString(s); }, s);
	}
	std::string getLabel() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_label", [this] { return Base::getLabel(); });
	}
	std::string getUnit() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_unit", [this] { return Base::getUnit(); });
	}
	std::string getString() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_string", [this] { return Base::getString(); });
	}
	void reset() override {
		ExclusiveBudget budget("Quantity.reset");
		overrideOrVoid(static_cast<const Base*>(this), "reset", [this] { Base::reset(); });
	}
	void randomize() override {
		ExclusiveBudget budget("Quantity.randomize");
		overrideOrVoid(static_cast<const Base*>(this), "randomize", [this] { Base::randomize(); });
	}
};

}  // namespace

/** RackPy's own concrete Quantity (not Rack API): stores its value natively, so the mixed methods
are safe on any thread, and Python configures it. Label and unit are UI-path strings behind a mutex;
the audio path never reads them. */
struct ValueQuantity : rack::Quantity {
	std::atomic<float> value{0.f};
	std::atomic<float> minValue{0.f};
	std::atomic<float> maxValue{1.f};
	std::atomic<float> defaultValue{0.f};
	mutable std::mutex textMutex;
	std::string label;
	std::string unit;

	ValueQuantity() = default;
	ValueQuantity(float minValue, float maxValue, float defaultValue, std::string label, std::string unit)
		: minValue(minValue), maxValue(maxValue), defaultValue(defaultValue), label(std::move(label)), unit(std::move(unit)) {
		value.store(rack::math::clampSafe(defaultValue, minValue, maxValue));
	}
	void setValue(float v) override {
		// As ParamQuantity::setValue: clampSafe to the bounds. fmin(NaN, max) is max, so NaN becomes the maximum.
		value.store(rack::math::clampSafe(v, getMinValue(), getMaxValue()), std::memory_order_relaxed);
	}
	float getValue() override { return value.load(std::memory_order_relaxed); }
	float getMinValue() override { return minValue.load(std::memory_order_relaxed); }
	float getMaxValue() override { return maxValue.load(std::memory_order_relaxed); }
	float getDefaultValue() override { return defaultValue.load(std::memory_order_relaxed); }
	std::string getLabel() override {
		std::lock_guard<std::mutex> lock(textMutex);
		return label;
	}
	std::string getUnit() override {
		std::lock_guard<std::mutex> lock(textMutex);
		return unit;
	}
	void setText(std::string* field, std::string s) {
		std::lock_guard<std::mutex> lock(textMutex);
		*field = std::move(s);
	}
};

void bindQuantity(py::module_ raw) {
	using Q = rack::Quantity;
	py::classh<Q, QuantityTrampoline<Q>>(raw, "Quantity",
		"rack::Quantity: a float value with bounds and labels, read and written through virtual methods.")
		.def(py::init<>())
		.def("set_value", &Q::setValue, py::arg("value"))
		.def("get_value", &Q::getValue)
		.def("get_min_value", &Q::getMinValue)
		.def("get_max_value", &Q::getMaxValue)
		.def("get_default_value", &Q::getDefaultValue)
		.def("get_display_value", &Q::getDisplayValue)
		.def("set_display_value", &Q::setDisplayValue, py::arg("display_value"))
		.def("get_display_precision", &Q::getDisplayPrecision)
		.def("get_display_value_string", &Q::getDisplayValueString)
		.def("set_display_value_string", &Q::setDisplayValueString, py::arg("s"),
			"Parses a display string, with Rack's formula language (tinyexpr: note names, dB, ...), and sets the "
			"display value; leaves the value unchanged if the string does not parse.")
		.def("get_label", &Q::getLabel)
		.def("get_unit", &Q::getUnit)
		.def("get_string", &Q::getString)
		.def("reset", &Q::reset)
		.def("randomize", &Q::randomize)
		.def("is_min", &Q::isMin)
		.def("is_max", &Q::isMax)
		.def("set_min", &Q::setMin)
		.def("set_max", &Q::setMax)
		.def("toggle", &Q::toggle)
		.def("move_value", &Q::moveValue, py::arg("delta_value"))
		.def("get_range", &Q::getRange)
		.def("is_bounded", &Q::isBounded)
		.def("to_scaled", &Q::toScaled, py::arg("value"))
		.def("from_scaled", &Q::fromScaled, py::arg("scaled_value"))
		.def("set_scaled_value", &Q::setScaledValue, py::arg("scaled_value"))
		.def("get_scaled_value", &Q::getScaledValue)
		.def("move_scaled_value", &Q::moveScaledValue, py::arg("delta_scaled_value"));

	py::module_ types = raw.def_submodule("_types", "RackPy's native helper types; see rackpy.types.");
	py::classh<ValueQuantity, Q, QuantityTrampoline<ValueQuantity>>(types, "ValueQuantity",
		"A Quantity that stores its value natively (safe from any thread), with bounds, label and unit set from "
		"Python. RackPy's own type, not Rack API.")
		.def(py::init<>())
		.def(py::init<float, float, float, std::string, std::string>(), py::arg("min_value") = 0.f,
			py::arg("max_value") = 1.f, py::arg("default_value") = 0.f, py::arg("label") = "", py::arg("unit") = "")
		.def_property("min_value", [](const ValueQuantity& q) { return q.minValue.load(); },
			[](ValueQuantity& q, float v) { q.minValue.store(v); })
		.def_property("max_value", [](const ValueQuantity& q) { return q.maxValue.load(); },
			[](ValueQuantity& q, float v) { q.maxValue.store(v); })
		.def_property("default_value", [](const ValueQuantity& q) { return q.defaultValue.load(); },
			[](ValueQuantity& q, float v) { q.defaultValue.store(v); })
		.def_property("label", [](ValueQuantity& q) { return q.ValueQuantity::getLabel(); },
			[](ValueQuantity& q, std::string s) { q.setText(&q.label, std::move(s)); })
		.def_property("unit", [](ValueQuantity& q) { return q.ValueQuantity::getUnit(); },
			[](ValueQuantity& q, std::string s) { q.setText(&q.unit, std::move(s)); });
}

}  // namespace values
}  // namespace rackpy
