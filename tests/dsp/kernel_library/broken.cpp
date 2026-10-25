// SPDX-License-Identifier: GPL-3.0-or-later
// Kernel libraries that RackPy must refuse (RP-17B tests), one per VARIANT_*.
#include <rack.hpp>
#include <rackpy/kernel.hpp>

#include <stdexcept>

namespace {

struct Nothing : rackpy::Kernel {
	bool process(rack::engine::Module&, const rack::engine::Module::ProcessArgs&) noexcept override { return true; }
};

rackpy::KernelType type(const char* name) {
	return rackpy::KernelType{name, "test", {}, {}, [](const std::vector<double>&) { return std::make_unique<Nothing>(); }};
}

}  // namespace

#if defined(VARIANT_WRONG_ABI)
extern "C" __attribute__((visibility("default"))) int rackpy_kernel_abi() { return 999; }
extern "C" __attribute__((visibility("default"))) const char* rackpy_kernel_namespace() { return "wrongabi"; }
extern "C" __attribute__((visibility("default"))) void rackpy_register_kernels(rackpy::KernelRegistrar& r) { r.add(type("Never")); }
#elif defined(VARIANT_THROWS)
RACKPY_KERNEL_LIBRARY("throws", registrar) {
	registrar.add(type("Partial"));  // must not be registered: the library fails as a whole
	throw std::runtime_error("kernel-library-intentional-error");
}
#elif defined(VARIANT_DUPLICATE)
RACKPY_KERNEL_LIBRARY("example", registrar) {
	registrar.add(type("Fold"));
}
#elif defined(VARIANT_RESERVED)
RACKPY_KERNEL_LIBRARY("rackpy", registrar) {
	registrar.add(type("Gain"));
}
#elif defined(VARIANT_BAD_NAME)
RACKPY_KERNEL_LIBRARY("badname", registrar) {
	registrar.add(type("Good"));
	registrar.add(type("not.good"));
}
#else
#error "build broken.cpp with one VARIANT_* definition"
#endif
