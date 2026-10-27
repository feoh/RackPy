// SPDX-License-Identifier: GPL-3.0-or-later
// Laboratory modules (RP-22A, RP-22B): Python overrides of a Module's
// virtuals, with the RackPy module's own implementations (Rack's, plus
// RackPy's lifecycle snapshot and data handlers) as their superclass.
#include <atomic>

#include "laboratory.hpp"
#include "../bindings/engine/module.hpp"
#include "../bindings/plugin.hpp"
#include "../bindings/support/hooks.hpp"
#include "../bindings/support/quantity.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/json.hpp"

namespace rackpy {
namespace laboratory {

const char* const OVERRIDE_NAMES[(size_t) Override::Count] = {
	"process",
	"process_bypass",
	"step",
	"on_add",
	"on_remove",
	"on_bypass",
	"on_un_bypass",
	"on_port_change",
	"on_reset",
	"on_randomize",
	"on_save",
	"on_sample_rate_change",
	"on_set_master",
	"on_unset_master",
	"on_expander_change",
	"module_to_json",
	"module_from_json",
	"params_to_json",
	"params_from_json",
	"data_to_json",
	"data_from_json",
};

namespace {

std::atomic<bool> laboratoryEnabled{false};

using M = rack::engine::Module;
using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

Module& laboratoryHandle(const bindings::ModuleRef& r) {
	Module* m = laboratoryModule(r.get());
	if (!m)
		throw runtime::Laboratory("this is not a laboratory module (rackpy.laboratory.ModuleClass.create makes one)");
	return *m;
}

/** Laboratory callbacks nested in this thread: a callback that calls Rack's code
that calls a callback, and so on. Each level holds C++ frames that Python's
own recursion limit does not see, so the laboratory bounds the nesting. */
constexpr int NESTING_LIMIT = 200;
thread_local int nesting = 0;

/** Call a Python override for Rack's code. Never throws: an error goes to the
Python caller (ErrorCapture), or is contained if Rack called. */
template <typename F>
void dispatch(Override o, F&& call) {
	const char* what = OVERRIDE_NAMES[(size_t) o];
	bindings::runPythonHook(what, [&] {
		if (nesting >= NESTING_LIMIT) {
			PyErr_Format(PyExc_RecursionError, "laboratory callbacks nested more than %d deep (in %s)", NESTING_LIMIT, what);
			throw py::error_already_set();
		}
		nesting++;
		struct Leave {
			~Leave() { nesting--; }
		} leave;
		call();
	});
}

/** Raise `message` to the Python caller from inside a virtual, without unwinding through Rack's code. */
void refuse(const char* message) {
	bindings::runPythonHook("laboratory Module", [&] { throw runtime::Laboratory(message); });
}

/** Run Rack's code for a Python caller: an override's error inside is raised here. */
template <typename F>
auto forPython(F&& f) {
	bindings::ErrorCapture capture;
	if constexpr (std::is_void_v<decltype(f())>) {
		f();
		capture.rethrow();
	}
	else {
		auto result = f();
		if constexpr (std::is_same_v<decltype(result), json_t*>) {
			JsonPtr owner(result, json_decref);
			capture.rethrow();
			return owner.release();
		}
		else {
			capture.rethrow();
			return result;
		}
	}
}

py::object jsonResult(json_t* json) {
	JsonPtr owner(json, json_decref);
	return json ? runtime::fromJson(json) : py::object(py::none());
}

const char* const NO_MODEL = "Rack's Module::toJson and fromJson name the module's model, and this laboratory module has "
	"none: override module_to_json/module_from_json, or create it with a model";

}  // namespace

bool enabled() {
	return laboratoryEnabled.load(std::memory_order_acquire);
}

void requireRackContext(const module::Module& m, const char* what) {
	if (m.paramQuantities.empty())
		return;
	auto context = runtime::binding().current();
	if (!context || rack::contextGet() != context->context || !context->context->engine)
		throw runtime::WrongContext(std::string(what) + " needs Rack's live bound UI context: Rack's parameter quantities "
			"read and write their values through Rack's engine");
}

void requireEnabled(const char* what) {
	if (!enabled())
		throw runtime::Laboratory(std::string(what) + " needs the callback laboratory, which is disabled: Python never runs "
			"on Rack's audio threads. Trusted code may call rackpy.laboratory.enable() to run Python callbacks offline "
			"on a laboratory Bench, for experiments and tests; that is not real-time audio");
}

// Every override runs where its caller is: a Bench operation or a Python call,
// on the main thread with the GIL, so an error goes back to that caller.
// Anything else (no GIL, another thread) is contained by callPython, which
// never runs Python on Rack's own threads.

void Module::process(const ProcessArgs& args) {
	if (const auto& f = override(Override::Process))
		dispatch(Override::Process, [&] { f.get()(ProcessArgs(args)); });
	else
		rack::engine::Module::process(args);  // Rack's: calls step() (laboratory modules take no kernel)
}

void Module::processBypass(const ProcessArgs& args) {
	if (const auto& f = override(Override::ProcessBypass))
		dispatch(Override::ProcessBypass, [&] { f.get()(ProcessArgs(args)); });
	else
		rack::engine::Module::processBypass(args);  // Rack's: the bypass routes
}

void Module::step() {
	if (const auto& f = override(Override::Step))
		dispatch(Override::Step, [&] { f.get()(); });
	else
		rack::engine::Module::step();
}

// Events: the Python override gets a copy of Rack's event; without one, the
// RackPy module's adapter runs (Rack's behaviour, then the snapshot).
#define RACKPY_LAB_EVENT(Method, Event, Kind)                                       \
	void Module::Method(const Event& e) {                                           \
		if (const auto& f = override(Override::Kind))                               \
			dispatch(Override::Kind, [&] { f.get()(Event(e)); });      \
		else                                                                        \
			module::Module::Method(e);                                              \
	}
RACKPY_LAB_EVENT(onAdd, AddEvent, OnAdd)
RACKPY_LAB_EVENT(onRemove, RemoveEvent, OnRemove)
RACKPY_LAB_EVENT(onBypass, BypassEvent, OnBypass)
RACKPY_LAB_EVENT(onUnBypass, UnBypassEvent, OnUnBypass)
RACKPY_LAB_EVENT(onPortChange, PortChangeEvent, OnPortChange)
RACKPY_LAB_EVENT(onReset, ResetEvent, OnReset)
RACKPY_LAB_EVENT(onRandomize, RandomizeEvent, OnRandomize)
RACKPY_LAB_EVENT(onSave, SaveEvent, OnSave)
RACKPY_LAB_EVENT(onSampleRateChange, SampleRateChangeEvent, OnSampleRateChange)
RACKPY_LAB_EVENT(onSetMaster, SetMasterEvent, OnSetMaster)
RACKPY_LAB_EVENT(onUnsetMaster, UnsetMasterEvent, OnUnsetMaster)
RACKPY_LAB_EVENT(onExpanderChange, ExpanderChangeEvent, OnExpanderChange)
#undef RACKPY_LAB_EVENT

// JSON: synchronous, as Rack requires. An override returns JSON-compatible
// data, or None for Rack's NULL; it receives Rack's JSON converted.
namespace {

json_t* jsonOverride(const Module& m, Override o) {
	json_t* result = nullptr;
	const auto& f = m.override(o);
	dispatch(o, [&] {
		py::object value = f.get()();
		if (!value.is_none())
			result = runtime::toJson(value);
	});
	return result;
}

void jsonInput(const Module& m, Override o, json_t* json) {
	const auto& f = m.override(o);
	dispatch(o, [&] { f.get()(runtime::fromJson(json)); });
}

}  // namespace

json_t* Module::toJson() {
	if (override(Override::ModuleToJson))
		return jsonOverride(*this, Override::ModuleToJson);
	if (!model) {
		refuse(NO_MODEL);
		return nullptr;
	}
	return module::Module::toJson();
}

void Module::fromJson(json_t* rootJ) {
	if (override(Override::ModuleFromJson))
		return jsonInput(*this, Override::ModuleFromJson, rootJ);
	if (!model)
		return refuse(NO_MODEL);
	module::Module::fromJson(rootJ);
}

json_t* Module::paramsToJson() {
	if (override(Override::ParamsToJson))
		return jsonOverride(*this, Override::ParamsToJson);
	return module::Module::paramsToJson();
}

void Module::paramsFromJson(json_t* rootJ) {
	if (override(Override::ParamsFromJson))
		return jsonInput(*this, Override::ParamsFromJson, rootJ);
	module::Module::paramsFromJson(rootJ);
}

json_t* Module::dataToJson() {
	if (override(Override::DataToJson))
		return jsonOverride(*this, Override::DataToJson);
	return module::Module::dataToJson();  // the data handlers (ModuleClass.to_json)
}

void Module::dataFromJson(json_t* rootJ) {
	if (override(Override::DataFromJson))
		return jsonInput(*this, Override::DataFromJson, rootJ);
	module::Module::dataFromJson(rootJ);
}

namespace {

/** `base` must call the RackPy module's implementation by qualified name: a
pointer to a virtual member would dispatch back to the override. */
template <typename Event, typename Base>
void bindBaseEvent(py::module_& lab, const char* name, const char* doc, Base base) {
	lab.def(name, [base](const bindings::ModuleRef& r, const Event& e) {
		Module& m = laboratoryHandle(r);
		forPython([&] { base(m, e); });
	}, py::arg("module"), py::arg("event"), doc);
}

}  // namespace

void bindModules(py::module_ lab) {
	lab.def("enable", [] {
		runtime::requireMainThread("enabling the callback laboratory");
		if (!laboratoryEnabled.exchange(true))
			WARN("RackPy: the callback laboratory is enabled: Python module callbacks run offline on laboratory "
				"benches, never in Rack's engine, and are not real-time audio");
	}, "Enable the callback laboratory for this process. Trusted code only; patches never enable it.");
	lab.def("disable", [] {
		runtime::requireMainThread("disabling the callback laboratory");
		laboratoryEnabled.store(false, std::memory_order_release);
	}, "Disable it again: creating laboratory modules and benches, and stepping benches, are refused.");
	lab.def("enabled", &enabled);
	py::tuple names((size_t) Override::Count);
	for (size_t i = 0; i < (size_t) Override::Count; i++)
		names[i] = OVERRIDE_NAMES[i];
	lab.attr("OVERRIDES") = names;

	lab.def("create_module", [](py::object configure, py::object model) {
		requireEnabled("creating a laboratory module");
		runtime::requireMainThread("creating a laboratory module");
		rack::plugin::Model* named = model.is_none() ? nullptr : bindings::modelArgument(model);
		py::object handle = bindings::createWith(std::make_shared<module::Construction>(std::make_unique<Module>()),
			std::move(configure));
		// As Rack's createModule: the model is set after the constructor.
		handle.cast<const bindings::ModuleRef&>().get().model = named;
		return handle;
	}, py::arg("configure"), py::arg("model") = py::none(),
		"Module.create(configure) for a laboratory module: a RackPy module that only a laboratory Bench runs. "
		"`model` only names it in Rack's JSON; Rack never creates or runs it.");
	lab.def("is_laboratory_module", [](const bindings::ModuleRef& r) { return laboratoryModule(r.get()) != nullptr; },
		py::arg("module"));
	lab.def("set_overrides", [](const bindings::ModuleRef& r, py::kwargs overrides) {
		r.state->requireConstruction();
		Module& m = laboratoryHandle(r);
		std::array<runtime::PyRef, (size_t) Override::Count> next;
		for (auto item : overrides) {
			std::string key = item.first.cast<std::string>();
			size_t i = 0;
			while (i < next.size() && key != OVERRIDE_NAMES[i])
				i++;
			if (i == next.size())
				throw py::type_error("set_overrides() got an unexpected override " + key);
			if (item.second.is_none())
				continue;
			if (!PyCallable_Check(item.second.ptr()))
				throw py::type_error(key + " must be callable or None");
			next[i] = runtime::PyRef(py::reinterpret_borrow<py::object>(item.second));
		}
		m.overrides = std::move(next);
	}, py::arg("module"),
		"Install a laboratory module's Python overrides by name (laboratory.OVERRIDES). Constructor-only; replaces "
		"earlier ones.");

	// The superclass implementations a Python override reaches with super().
	lab.def("base_process", [](const bindings::ModuleRef& r, const M::ProcessArgs& args) {
		Module& m = laboratoryHandle(r);
		forPython([&] { m.rack::engine::Module::process(args); });
	}, py::arg("module"), py::arg("args"), "Rack's Module::process: calls step().");
	lab.def("base_process_bypass", [](const bindings::ModuleRef& r, const M::ProcessArgs& args) {
		Module& m = laboratoryHandle(r);
		forPython([&] { m.rack::engine::Module::processBypass(args); });
	}, py::arg("module"), py::arg("args"), "Rack's Module::processBypass: copies each bypass route's input to its output.");
	lab.def("base_step", [](const bindings::ModuleRef& r) {
		Module& m = laboratoryHandle(r);
		forPython([&] { m.rack::engine::Module::step(); });
	}, py::arg("module"), "Rack's deprecated Module::step: does nothing.");
	bindBaseEvent<M::AddEvent>(lab, "base_on_add", "Rack's onAdd, and RackPy's snapshot.", [](Module& m, const M::AddEvent& e) { m.module::Module::onAdd(e); });
	bindBaseEvent<M::RemoveEvent>(lab, "base_on_remove", "Rack's onRemove, and the snapshot.", [](Module& m, const M::RemoveEvent& e) { m.module::Module::onRemove(e); });
	bindBaseEvent<M::BypassEvent>(lab, "base_on_bypass", "Rack's onBypass, and the snapshot.", [](Module& m, const M::BypassEvent& e) { m.module::Module::onBypass(e); });
	bindBaseEvent<M::UnBypassEvent>(lab, "base_on_un_bypass", "Rack's onUnBypass, and the snapshot.", [](Module& m, const M::UnBypassEvent& e) { m.module::Module::onUnBypass(e); });
	bindBaseEvent<M::PortChangeEvent>(lab, "base_on_port_change", "Rack's onPortChange, and the snapshot.", [](Module& m, const M::PortChangeEvent& e) { m.module::Module::onPortChange(e); });
	bindBaseEvent<M::ResetEvent>(lab, "base_on_reset", "Rack's onReset: resets every resettable bounded parameter; and the snapshot.", [](Module& m, const M::ResetEvent& e) { m.module::Module::onReset(e); });
	bindBaseEvent<M::RandomizeEvent>(lab, "base_on_randomize", "Rack's onRandomize: randomizes every randomizable bounded parameter; and the snapshot.", [](Module& m, const M::RandomizeEvent& e) { m.module::Module::onRandomize(e); });
	bindBaseEvent<M::SaveEvent>(lab, "base_on_save", "Rack's onSave, and the snapshot.", [](Module& m, const M::SaveEvent& e) { m.module::Module::onSave(e); });
	bindBaseEvent<M::SampleRateChangeEvent>(lab, "base_on_sample_rate_change", "Rack's onSampleRateChange (which calls the deprecated no-argument one), and the snapshot.", [](Module& m, const M::SampleRateChangeEvent& e) { m.module::Module::onSampleRateChange(e); });
	bindBaseEvent<M::SetMasterEvent>(lab, "base_on_set_master", "Rack's onSetMaster, and the snapshot.", [](Module& m, const M::SetMasterEvent& e) { m.module::Module::onSetMaster(e); });
	bindBaseEvent<M::UnsetMasterEvent>(lab, "base_on_unset_master", "Rack's onUnsetMaster, and the snapshot.", [](Module& m, const M::UnsetMasterEvent& e) { m.module::Module::onUnsetMaster(e); });
	bindBaseEvent<M::ExpanderChangeEvent>(lab, "base_on_expander_change", "Rack's onExpanderChange (nothing), and the snapshot.", [](Module& m, const M::ExpanderChangeEvent& e) { m.module::Module::onExpanderChange(e); });

	lab.def("base_module_to_json", [](const bindings::ModuleRef& r) {
		Module& m = laboratoryHandle(r);
		if (!m.model)
			throw runtime::Laboratory(NO_MODEL);
		requireRackContext(m, "Rack's Module::toJson");
		return jsonResult(forPython([&] { return m.module::Module::toJson(); }));
	}, py::arg("module"), "Rack's Module::toJson: id, plugin, model, version, params, bypass, expanders, data.");
	lab.def("base_module_from_json", [](const bindings::ModuleRef& r, py::handle data) {
		Module& m = laboratoryHandle(r);
		if (!m.model)
			throw runtime::Laboratory(NO_MODEL);
		requireRackContext(m, "Rack's Module::fromJson");
		JsonPtr json(runtime::toJson(data), json_decref);
		forPython([&] { m.module::Module::fromJson(json.get()); });
	}, py::arg("module"), py::arg("data"), "Rack's Module::fromJson.");
	lab.def("base_params_to_json", [](const bindings::ModuleRef& r) {
		Module& m = laboratoryHandle(r);
		requireRackContext(m, "Rack's Module::paramsToJson");
		return jsonResult(forPython([&] { return m.module::Module::paramsToJson(); }));
	}, py::arg("module"), "Rack's Module::paramsToJson: each bounded parameter quantity's JSON with its ID.");
	lab.def("base_params_from_json", [](const bindings::ModuleRef& r, py::handle data) {
		Module& m = laboratoryHandle(r);
		requireRackContext(m, "Rack's Module::paramsFromJson");
		JsonPtr json(runtime::toJson(data), json_decref);
		forPython([&] { m.module::Module::paramsFromJson(json.get()); });
	}, py::arg("module"), py::arg("data"), "Rack's Module::paramsFromJson.");
	lab.def("base_data_to_json", [](const bindings::ModuleRef& r) {
		Module& m = laboratoryHandle(r);
		return jsonResult(forPython([&] { return m.module::Module::dataToJson(); }));
	}, py::arg("module"), "The module's data handlers (ModuleClass.to_json); Rack's NULL without them.");
	lab.def("base_data_from_json", [](const bindings::ModuleRef& r, py::handle data) {
		Module& m = laboratoryHandle(r);
		JsonPtr json(runtime::toJson(data), json_decref);
		forPython([&] { m.module::Module::dataFromJson(json.get()); });
	}, py::arg("module"), py::arg("data"), "The module's data handlers (ModuleClass.from_json); nothing without them.");
}

void bindQuantities(py::module_ lab) {
	lab.def("mark_quantity", [](rack::Quantity& q) {
		requireEnabled("creating a laboratory quantity");
		runtime::requireMainThread("creating a laboratory quantity");
		auto* flag = dynamic_cast<bindings::LaboratoryQuantity*>(&q);
		if (!flag)
			throw py::type_error("only a Quantity, ParamQuantity or SwitchQuantity made in Python can be a laboratory quantity");
		if (auto* pq = dynamic_cast<rack::engine::ParamQuantity*>(&q); pq && pq->module)
			throw runtime::Laboratory("a quantity becomes a laboratory quantity only as it is created, before it has a module");
		flag->laboratoryOverrides = true;
	}, py::arg("quantity"), "rackpy.laboratory's quantity classes call this from __init__: their value methods become Python.");
	lab.def("is_laboratory_quantity", [](rack::Quantity& q) {
		auto* flag = dynamic_cast<bindings::LaboratoryQuantity*>(&q);
		return flag && flag->laboratoryOverrides;
	}, py::arg("quantity"));
}

void bind(py::module_ host) {
	py::module_ lab = host.def_submodule("laboratory", "The callback laboratory (RP-22); use rackpy.laboratory.");
	bindModules(lab);
	bindQuantities(lab);
	bindMidi(lab);
	bindAudio(lab);
	bindBench(lab);
	runtime::onFinalize([] { laboratoryEnabled.store(false); });
}

}  // namespace laboratory
}  // namespace rackpy
