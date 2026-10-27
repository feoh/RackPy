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
#include <functional>
#include <vector>

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

/** The Python objects of every live laboratory MIDI driver and device (rackpy.laboratory.MidiDriver,
MidiInputDevice, MidiOutputDevice), appended to `out`, skipping any being deallocated. Needs the GIL. */
void liveMidiObjects(std::vector<py::object>& out);
/** The same for the laboratory audio drivers and devices (AudioDriver, AudioDevice). */
void liveAudioObjects(std::vector<py::object>& out);

// Keeping the laboratory's drivers and devices alive while Rack's code uses them (RP-22F). Rack's port code
// calls a driver or device again after an override ran (Port::setDeviceId unsubscribes, then subscribes through
// the same driver; InputDevice::onMessage iterates its subscribers), and an override may drop the last reference
// to that driver or device. So whenever a laboratory override runs on a subscription path or a synchronous MIDI
// delivery, every live laboratory driver and device is kept alive (keepLaboratoryObjectsAlive), and released
// only when no such Rack code can still be running on the main thread: at the end of the outermost
// LaboratoryCall, at the next UI step, at context close or at finalization. Main thread; needs the GIL.

/** Keep every live laboratory driver and device, and `extra` (if any), alive until released. */
void keepLaboratoryObjectsAlive(py::object extra = py::object());
/** Release what was kept, unless a LaboratoryCall, a subscription dispatch or a MIDI delivery is in progress. */
void releaseKeptLaboratoryObjects() noexcept;

/** Wraps a Python call into Rack's port, device or driver code that may reach laboratory overrides (a port's
set_device_id, a device's push_message, ...): what overrides kept alive goes when the outermost call returns. */
struct LaboratoryCall {
	LaboratoryCall();
	~LaboratoryCall();
	LaboratoryCall(const LaboratoryCall&) = delete;
	LaboratoryCall& operator=(const LaboratoryCall&) = delete;
};

/** Held while a laboratory override runs on a subscription path (subscribe, unsubscribe): nothing kept is
released until Rack's port code, which called it, has returned. */
struct SubscriptionDispatch {
	SubscriptionDispatch();
	~SubscriptionDispatch();
	SubscriptionDispatch(const SubscriptionDispatch&) = delete;
	SubscriptionDispatch& operator=(const SubscriptionDispatch&) = delete;
};

/** Run `deleter` now on the main thread; from another thread (a Python thread dropping the last reference),
at the main thread's next UI step, context close or finalization, under the GIL: Rack's UI reads the
laboratory drivers and devices on the main thread without the GIL, so none may be destroyed elsewhere. */
void deleteOnMainThreadLater(std::function<void()> deleter);

/** The deleter of every laboratory driver and device (the factories' unique_ptr). */
template <class T>
struct MainThreadDeleteLater {
	void operator()(T* object) const noexcept {
		deleteOnMainThreadLater([object] { delete object; });
	}
};

/** _rackpy_host.laboratory: enablement, laboratory modules and the Bench. */
void bind(py::module_ host);
void bindModules(py::module_ lab);
void bindQuantities(py::module_ lab);
void bindMidi(py::module_ lab);
void bindAudio(py::module_ lab);
void bindBench(py::module_ lab);
/** The keep-alive and deferred-destruction hooks (keepalive.cpp). */
void bindKeepAlive();

}  // namespace laboratory
}  // namespace rackpy
