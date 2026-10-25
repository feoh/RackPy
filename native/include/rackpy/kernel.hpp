// SPDX-License-Identifier: GPL-3.0-or-later
// RackPy native DSP kernels (RP-17): the production audio path of a module
// whose configuration Python wrote. Rack calls a module's process() once per
// sample; RackPy calls the module's kernel there, on Rack's engine thread,
// and never runs Python on that path.
//
// This header is the interface for built-in kernels and, later, for kernel
// libraries that plugin authors build with the Rack SDK (owner decision
// 2026-10-24). Include it after <rack.hpp>; it uses only Rack's public API.
#pragma once
#include <rack.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rackpy {

/** Bumped whenever Kernel, KernelType, Field or KernelRegistrar change incompatibly. */
constexpr int KERNEL_ABI = 1;

/** One kernel instance, owned by one module.

Real-time rules for everything marked "engine thread": no heap allocation or
free, no locks, no IO or logging, no exceptions (the methods are noexcept),
no Python. Allocate in the constructor, on the main thread. */
struct Kernel {
	virtual ~Kernel() = default;
	/** One frame, on Rack's engine thread. Read and write the module's
	ports, params and lights directly. Return false to fail: RackPy then
	silences the module's outputs, records a diagnostic, and stops calling
	this kernel until Python sets another one. */
	virtual bool process(rack::engine::Module& module, const rack::engine::Module::ProcessArgs& args) noexcept = 0;
	/** Rack's reset (the module's Initialize), with the engine stopped. */
	virtual void reset() noexcept {}
	/** Rack's sample rate changed; also called once before the first frame
	this kernel processes after a replacement. Engine thread or engine stopped. */
	virtual void sampleRateChange(float sampleRate, float sampleTime) noexcept {}
	/** A control value Python sent (KernelType::controls[slot]), applied on
	the engine thread at a frame boundary, before process(). */
	virtual void setControl(int slot, double value) noexcept {}
};

/** What a kernel type needs from a module, by name. Python passes these as
keyword arguments; RackPy checks wiring against the module's configuration
before the kernel is attached. */
struct Field {
	enum Kind { INPUT, OUTPUT, PARAM, LIGHT, NUMBER };
	std::string name;
	Kind kind;
	/** For a port, param or light, -1 means "not connected", allowed only if the field is optional. */
	double defaultValue = 0.0;
	bool required = false;
	std::string doc;
};

/** A kind of kernel that Python can create by name. */
struct KernelType {
	/** "namespace.Name", e.g. "rackpy.Gain". */
	std::string name;
	std::string doc;
	std::vector<Field> fields;
	/** Names of NUMBER fields Python may change later with Kernel.set(); a slot is an index here. */
	std::vector<std::string> controls;
	/** Main thread. `values` follows `fields` (indices as whole numbers). Throw
	std::invalid_argument for values the type cannot use. */
	std::function<std::unique_ptr<Kernel>(const std::vector<double>& values)> create;
};

/** What a kernel library's registration function adds its types to. */
struct KernelRegistrar {
	/** Add a type. Its name is short ("Fold"); Python sees it as "namespace.Fold". */
	virtual void add(KernelType type) = 0;

protected:
	~KernelRegistrar() = default;
};

}  // namespace rackpy

/** Defines a kernel library's entry points (RP-17B). Build the library with
the Rack SDK's flags, list it in the plugin's rackpy-loader.json "kernels",
and RackPy loads it before the entry module runs:

	RACKPY_KERNEL_LIBRARY("myplugin", registrar) {
		registrar.add(rackpy::KernelType{"Fold", "...", {...}, {...}, create});
	}
*/
#define RACKPY_KERNEL_LIBRARY(NAMESPACE, REGISTRAR) \
	extern "C" __attribute__((visibility("default"))) int rackpy_kernel_abi() { return rackpy::KERNEL_ABI; } \
	extern "C" __attribute__((visibility("default"))) const char* rackpy_kernel_namespace() { return NAMESPACE; } \
	extern "C" __attribute__((visibility("default"))) void rackpy_register_kernels(rackpy::KernelRegistrar& REGISTRAR)
