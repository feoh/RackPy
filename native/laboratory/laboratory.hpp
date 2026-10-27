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

#include <array>
#include <cstddef>

#include "../module/module.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace laboratory {

namespace py = pybind11;

/** Whether trusted code enabled the laboratory (rackpy.laboratory.enable()). */
bool enabled();
/** Throws runtime::Laboratory unless the laboratory is enabled. */
void requireEnabled(const char* what);
/** Throws WrongContext for a module with parameters outside Rack's live bound UI context:
Rack's ParamQuantity reads and writes values through APP->engine. */
void requireRackContext(const module::Module& m, const char* what);

/** The Module virtuals a laboratory module may override in Python, by name. */
enum class Override : size_t {
	Process,
	ProcessBypass,
	Step,
	OnAdd,
	OnRemove,
	OnBypass,
	OnUnBypass,
	OnPortChange,
	OnReset,
	OnRandomize,
	OnSave,
	OnSampleRateChange,
	OnSetMaster,
	OnUnsetMaster,
	OnExpanderChange,
	ModuleToJson,
	ModuleFromJson,
	ParamsToJson,
	ParamsFromJson,
	DataToJson,
	DataFromJson,
	Count,
};

/** The Python names, in Override order (rackpy.laboratory.ModuleClass's methods). */
extern const char* const OVERRIDE_NAMES[(size_t) Override::Count];

/** A laboratory module: a RackPy module whose virtuals dispatch to Python
overrides. Python calls reach it only from a Bench, a `base_*` function or a
rackpy.raw.engine.Module method, with the GIL held on the main thread; an
override's error is kept for that caller (bindings::ErrorCapture). Without an
override, the RackPy module's own implementation runs: Rack's, with RackPy's
snapshot and data handlers. */
struct Module : module::Module {
	Module() : module::Module(true) {}
	std::array<runtime::PyRef, (size_t) Override::Count> overrides;

	const runtime::PyRef& override(Override o) const { return overrides[(size_t) o]; }

	void process(const ProcessArgs& args) override;
	void processBypass(const ProcessArgs& args) override;
	void step() override;
	void onAdd(const AddEvent& e) override;
	void onRemove(const RemoveEvent& e) override;
	void onBypass(const BypassEvent& e) override;
	void onUnBypass(const UnBypassEvent& e) override;
	void onPortChange(const PortChangeEvent& e) override;
	void onReset(const ResetEvent& e) override;
	void onRandomize(const RandomizeEvent& e) override;
	void onSave(const SaveEvent& e) override;
	void onSampleRateChange(const SampleRateChangeEvent& e) override;
	void onSetMaster(const SetMasterEvent& e) override;
	void onUnsetMaster(const UnsetMasterEvent& e) override;
	void onExpanderChange(const ExpanderChangeEvent& e) override;
	json_t* toJson() override;
	void fromJson(json_t* rootJ) override;
	json_t* paramsToJson() override;
	void paramsFromJson(json_t* rootJ) override;
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;
	using module::Module::onAdd;
	using module::Module::onRemove;
	using module::Module::onReset;
	using module::Module::onRandomize;
	using module::Module::onSampleRateChange;

	/** The Python references it holds, for its handle's garbage-collector traversal. */
	template <typename Visit>
	void visit(Visit&& f) const {
		for (const runtime::PyRef& ref : overrides)
			f(ref.borrowed());
	}
	void clearOverrides() {
		for (runtime::PyRef& ref : overrides)
			ref.reset();
	}
};

/** The laboratory module inside a RackPy module, or null for an ordinary one. */
inline Module* laboratoryModule(module::Module& m) {
	return m.laboratory ? static_cast<Module*>(&m) : nullptr;
}

/** _rackpy_host.laboratory: enablement, laboratory modules and the Bench. */
void bind(py::module_ host);
void bindModules(py::module_ lab);
void bindQuantities(py::module_ lab);
void bindMidi(py::module_ lab);
void bindAudio(py::module_ lab);
void bindBench(py::module_ lab);

}  // namespace laboratory
}  // namespace rackpy
