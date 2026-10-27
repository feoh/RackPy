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
// - value/range methods on plain quantities dispatch Python only on the main
//   thread; elsewhere the native base answers with a bounded diagnostic.
//   ParamQuantity's engine-reachable methods retain their native path.
#include <atomic>
#include <mutex>
#include <string>

#include "../support/quantity.hpp"
#include "values.hpp"

namespace rackpy {
namespace values {

using bindings::QuantityTrampoline;

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
		.def("set_value", [](Q& q, float value) { return bindings::quantityTrampoline(q, true) ? q.Q::setValue(value) : q.setValue(value); }, py::arg("value"))
		.def("get_value", [](Q& q) { return bindings::quantityTrampoline(q, true) ? q.Q::getValue() : q.getValue(); })
		.def("get_min_value", [](Q& q) { return bindings::quantityTrampoline(q, false) ? q.Q::getMinValue() : q.getMinValue(); })
		.def("get_max_value", [](Q& q) { return bindings::quantityTrampoline(q, false) ? q.Q::getMaxValue() : q.getMaxValue(); })
		.def("get_default_value", [](Q& q) { return bindings::quantityTrampoline(q, false) ? q.Q::getDefaultValue() : q.getDefaultValue(); })
		.def("get_display_value", [](Q& q) { return bindings::quantityTrampoline(q, true) ? q.Q::getDisplayValue() : q.getDisplayValue(); })
		.def("set_display_value", [](Q& q, float value) { return bindings::quantityTrampoline(q, true) ? q.Q::setDisplayValue(value) : q.setDisplayValue(value); }, py::arg("display_value"))
		.def("get_display_precision", [](Q& q) { return bindings::quantityTrampoline(q, false) ? q.Q::getDisplayPrecision() : q.getDisplayPrecision(); })
		.def("get_display_value_string", [](Q& q) { return bindings::quantityTrampoline(q, true) ? q.Q::getDisplayValueString() : q.getDisplayValueString(); })
		.def("set_display_value_string", [](Q& q, std::string s) { return bindings::quantityTrampoline(q, true) ? q.Q::setDisplayValueString(std::move(s)) : q.setDisplayValueString(std::move(s)); }, py::arg("s"),
			"Parses a display string, with Rack's formula language (tinyexpr: note names, dB, ...), and sets the "
			"display value; leaves the value unchanged if the string does not parse.")
		.def("get_label", [](Q& q) { return bindings::quantityTrampoline(q, false) ? q.Q::getLabel() : q.getLabel(); })
		.def("get_unit", [](Q& q) { return bindings::quantityTrampoline(q, false) ? q.Q::getUnit() : q.getUnit(); })
		.def("get_string", [](Q& q) { return bindings::quantityTrampoline(q, true) ? q.Q::getString() : q.getString(); })
		.def("reset", [](Q& q) { return bindings::quantityTrampoline(q, true) ? q.Q::reset() : q.reset(); })
		.def("randomize", [](Q& q) { return bindings::quantityTrampoline(q, true) ? q.Q::randomize() : q.randomize(); })
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
		.def("get_value", [](ValueQuantity& q) { return bindings::quantityTrampoline(q) ? q.ValueQuantity::getValue() : q.getValue(); })
		.def("set_value", [](ValueQuantity& q, float value) { return bindings::quantityTrampoline(q) ? q.ValueQuantity::setValue(value) : q.setValue(value); }, py::arg("value"))
		.def("get_min_value", [](ValueQuantity& q) { return bindings::quantityTrampoline(q) ? q.ValueQuantity::getMinValue() : q.getMinValue(); })
		.def("get_max_value", [](ValueQuantity& q) { return bindings::quantityTrampoline(q) ? q.ValueQuantity::getMaxValue() : q.getMaxValue(); })
		.def("get_default_value", [](ValueQuantity& q) { return bindings::quantityTrampoline(q) ? q.ValueQuantity::getDefaultValue() : q.getDefaultValue(); })
		.def("get_label", [](ValueQuantity& q) { return bindings::quantityTrampoline(q) ? q.ValueQuantity::getLabel() : q.getLabel(); })
		.def("get_unit", [](ValueQuantity& q) { return bindings::quantityTrampoline(q) ? q.ValueQuantity::getUnit() : q.getUnit(); })
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
