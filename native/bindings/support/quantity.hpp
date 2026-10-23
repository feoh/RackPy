// SPDX-License-Identifier: GPL-3.0-or-later
// UI-safe Quantity virtuals shared by standalone and native value quantities.
#pragma once
#include <chrono>
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

template <typename Base, typename Guard = UnguardedQuantity>
struct QuantityTrampoline : Base, py::trampoline_self_life_support {
	using Base::Base;
	// Standalone attached quantities additionally guard engine-dependent UI
	// calls. The default preserves Quantity/ValueQuantity's existing domain.
	virtual void checkAccess(bool = false) const { Guard::check(); }
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
