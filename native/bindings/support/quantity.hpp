// SPDX-License-Identifier: GPL-3.0-or-later
// UI-safe Quantity virtuals shared by standalone and native value quantities.
#pragma once
#include <atomic>
#include <chrono>
#include <type_traits>
#include "overrides.hpp"
#include "rackpy/runtime/diagnostics.hpp"

namespace rackpy {
namespace bindings {
namespace rt = rackpy::runtime;

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

/** Shared UI-safe virtuals. Base is Quantity, ValueQuantity or a parameter quantity. */
struct UnguardedQuantity { static void check() {} };

/** Set only by the callback laboratory's quantity classes (RP-22C, native/laboratory), and
only before the object can reach a module: its value and range methods then dispatch to
Python overrides. Rack's engine thread never reaches such an object (a laboratory
parameter quantity is accepted only by laboratory modules, which Rack's engine never
runs); were it to, callPython would refuse and Rack's implementation would answer. */
struct LaboratoryQuantity {
	virtual ~LaboratoryQuantity() = default;
	bool laboratoryOverrides = false;
	virtual void checkAccess(bool = false) const {}
	virtual void checkValueWrite() const { checkAccess(true); }
};

/** Base bindings bypass Python aliases only, never another plugin's C++ override.
Keep access checks which would otherwise have run in the trampoline. */
inline bool quantityTrampoline(rack::Quantity& q, bool engine = false, bool writing = false) {
	if (auto* alias = dynamic_cast<LaboratoryQuantity*>(&q)) {
		if (writing) alias->checkValueWrite();
		else alias->checkAccess(engine);
		return true;
	}
	return false;
}

template <typename Base, typename Guard = UnguardedQuantity>
struct QuantityTrampoline : Base, LaboratoryQuantity, py::trampoline_self_life_support {
	using Base::Base;
	// Plain quantities can dispatch values on the UI thread. Parameter quantities
	// may be called by the engine and retain native production semantics.
	std::atomic_flag reportedOffThread = ATOMIC_FLAG_INIT;
	bool dispatchValue(const char* name) {
		if (laboratoryOverrides)
			return true;
		if constexpr (std::is_base_of<rack::engine::ParamQuantity, Base>::value)
			return false;
		if (rt::isMainThread())
			return true;
		// A single bounded diagnostic per object, without touching Python on Rack threads.
		if (!reportedOffThread.test_and_set(std::memory_order_relaxed))
			rt::detail::noteRefused(name);
		return false;
	}
	float getValue() override {
		if (!dispatchValue("Quantity.get_value"))
			return Base::getValue();
		return overrideOr<float>(static_cast<const Base*>(this), "get_value", [this] { return Base::getValue(); });
	}
	void setValue(float v) override {
		if (!dispatchValue("Quantity.set_value"))
			return Base::setValue(v);
		overrideOrVoid(static_cast<const Base*>(this), "set_value", [this, v] { Base::setValue(v); }, v);
	}
	float getMinValue() override {
		if (!dispatchValue("Quantity.get_min_value"))
			return Base::getMinValue();
		return overrideOr<float>(static_cast<const Base*>(this), "get_min_value", [this] { return Base::getMinValue(); });
	}
	float getMaxValue() override {
		if (!dispatchValue("Quantity.get_max_value"))
			return Base::getMaxValue();
		return overrideOr<float>(static_cast<const Base*>(this), "get_max_value", [this] { return Base::getMaxValue(); });
	}
	float getDefaultValue() override {
		if (!dispatchValue("Quantity.get_default_value"))
			return Base::getDefaultValue();
		return overrideOr<float>(static_cast<const Base*>(this), "get_default_value",
			[this] { return Base::getDefaultValue(); });
	}
	// Standalone attached quantities additionally guard engine-dependent UI
	// calls. The default preserves Quantity/ValueQuantity's existing domain.
	void checkAccess(bool = false) const override { Guard::check(); }
	float getDisplayValue() override {
		checkAccess(true);
		return overrideOr<float>(static_cast<const Base*>(this), "get_display_value", [this] { return Base::getDisplayValue(); });
	}
	void setDisplayValue(float v) override {
		checkAccess(true);
		overrideOrVoid(static_cast<const Base*>(this), "set_display_value", [this, v] { Base::setDisplayValue(v); }, v);
	}
	int getDisplayPrecision() override {
		checkAccess();
		return overrideOr<int>(static_cast<const Base*>(this), "get_display_precision", [this] { return Base::getDisplayPrecision(); });
	}
	std::string getDisplayValueString() override {
		checkAccess(true);
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_display_value_string", [this] { return Base::getDisplayValueString(); });
	}
	void setDisplayValueString(std::string s) override {
		checkAccess(true);
		overrideOrVoid(static_cast<const Base*>(this), "set_display_value_string", [this, s] { Base::setDisplayValueString(s); }, s);
	}
	std::string getLabel() override {
		checkAccess();
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_label", [this] { return Base::getLabel(); });
	}
	std::string getUnit() override {
		checkAccess();
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_unit", [this] { return Base::getUnit(); });
	}
	std::string getString() override {
		checkAccess(true);
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_string", [this] { return Base::getString(); });
	}
	void reset() override {
		checkAccess(true);
		ExclusiveBudget budget("Quantity.reset");
		overrideOrVoid(static_cast<const Base*>(this), "reset", [this] { Base::reset(); });
	}
	void randomize() override {
		checkAccess(true);
		ExclusiveBudget budget("Quantity.randomize");
		overrideOrVoid(static_cast<const Base*>(this), "randomize", [this] { Base::randomize(); });
	}
};

}  // namespace bindings
}  // namespace rackpy
