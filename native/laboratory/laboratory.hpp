// SPDX-License-Identifier: GPL-3.0-or-later
// The callback laboratory (RP-22): Python overrides of the Module virtuals
// that Rack calls on its engine threads, run offline on a laboratory Bench on
// the main thread. Rack's engine never runs a laboratory module: Module's
// doProcess, setBypassed and setExpanderModule and Engine's constructor are
// PRIVATE, so no public-API host can run Python process() inside Rack's real
// engine (planning/rp-22-slices.md). Nothing here is on a production path.
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include "../module/module.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace laboratory {

namespace py = pybind11;

/** Whether trusted code enabled the laboratory (rackpy.laboratory.enable()). */
bool enabled();
/** Throws runtime::Laboratory unless the laboratory is enabled. */
void requireEnabled(const char* what);

/** A laboratory module: a RackPy module whose engine-thread virtuals dispatch to
Python overrides. Python calls reach it only from a Bench or a `base_*`
function, with the GIL held on the main thread; an override's error is kept
for that caller (bindings::ErrorCapture). Without an override, Rack's own
rack::engine::Module implementation runs, as for a C++ subclass. */
struct Module : module::Module {
	Module() : module::Module(true) {}
	runtime::PyRef processFn, processBypassFn, stepFn;

	void process(const ProcessArgs& args) override;
	void processBypass(const ProcessArgs& args) override;
	void step() override;

	/** The Python references it holds, for its handle's garbage-collector traversal. */
	template <typename Visit>
	void visit(Visit&& f) const {
		for (const runtime::PyRef* ref : {&processFn, &processBypassFn, &stepFn})
			f(ref->borrowed());
	}
	void clearOverrides() {
		processFn.reset();
		processBypassFn.reset();
		stepFn.reset();
	}
};

/** The laboratory module inside a RackPy module, or null for an ordinary one. */
inline Module* laboratoryModule(module::Module& m) {
	return m.laboratory ? static_cast<Module*>(&m) : nullptr;
}

/** _rackpy_host.laboratory: enablement, laboratory modules and the Bench. */
void bind(py::module_ host);
void bindModules(py::module_ lab);
void bindBench(py::module_ lab);

}  // namespace laboratory
}  // namespace rackpy
