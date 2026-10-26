// SPDX-License-Identifier: GPL-3.0-or-later
// rack::engine::Engine -> rackpy.raw.engine.Engine (Context.engine), and
// EngineModule handles for the modules in Rack's engine that RackPy did not
// create (RP-24A). Main thread. Every call that takes Rack's engine lock does
// so with the GIL released (ADR 003, decision 2) and is refused inside a
// callback that already holds it exclusively.
//
// Ownership follows Rack's: the engine holds modules but a module widget owns
// each one, so a module leaves the engine only through its widget, unless it
// is a widgetless RackPy module, which Python takes back. Engine::clear and
// Engine::fromJson delete what the engine holds, which only Rack's patch
// manager may do, after clearing the rack's widgets.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cmath>
#include <vector>

#include "../../module/module.hpp"
#include "../plugin.hpp"
#include "engine.hpp"
#include "module.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/json.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
using rack::engine::Engine;
using RackModule = rack::engine::Module;

namespace {

/** Context::engine, checked: the bound context's engine, on its main thread. */
struct EngineHandle {
	std::shared_ptr<runtime::ContextToken> token;
	Engine& get() const {
		runtime::requireMainThread("using Rack's engine");
		if (!token->alive())
			throw runtime::StaleHandle("Rack's context has closed");
		runtime::checkContext(*token);
		if (!token->context->engine)
			throw runtime::StaleHandle("Rack's engine has been deleted");
		return *token->context->engine;
	}
};

/** A module in Rack's engine that RackPy did not create: found again by its ID at every use. */
struct EngineModule {
	std::shared_ptr<runtime::ContextToken> token;
	int64_t id;
	RackModule* find() const {
		runtime::requireMainThread("using an engine module");
		if (!token->alive() || !token->context->engine)
			return nullptr;
		runtime::refuseEngineReentry("finding an engine module");
		py::gil_scoped_release release;
		runtime::EngineLease lease(*token);
		return token->context->engine->getModule(id);
	}
	RackModule& get() const {
		RackModule* m = find();
		if (!m)
			throw runtime::StaleHandle("module " + std::to_string(id) + " is no longer in Rack's engine");
		return *m;
	}
};

/** A Python module argument: a RackPy Module handle or an EngineModule. */
RackModule& moduleArgument(py::handle value) {
	if (py::isinstance<ModuleRef>(value))
		return value.cast<const ModuleRef&>().get();
	if (py::isinstance<EngineModule>(value))
		return value.cast<const EngineModule&>().get();
	throw py::type_error("expected a rackpy.raw.engine.Module or EngineModule");
}

/** Runs a locking engine call: refused inside an exclusive engine callback, then without the GIL. */
template <class F>
auto locked(const EngineHandle& h, const char* what, F f) {
	Engine& engine = h.get();
	runtime::refuseEngineReentry(what);
	py::gil_scoped_release release;
	runtime::EngineLease lease(*h.token);
	return f(engine);
}

/** A module argument that must be in Rack's engine. */
RackModule& engineModule(const EngineHandle& h, py::handle value) {
	RackModule& m = moduleArgument(value);
	if (!locked(h, "checking a module", [&m](Engine& e) { return e.hasModule(&m); }))
		throw runtime::Ownership("the module is not in Rack's engine");
	return m;
}

void requireParam(RackModule& m, int paramId) {
	if (paramId < 0 || static_cast<size_t>(paramId) >= m.params.size())
		throw std::invalid_argument("param ID " + std::to_string(paramId) + " is out of range for this module (" +
			std::to_string(m.params.size()) + ")");
}

/** Clearing or loading the engine deletes its modules and cables, so Rack clears its rack first. */
void requireEmptyRack(const EngineHandle& h, const char* what) {
	auto* scene = h.token->context->scene;
	if (scene && scene->rack && (!scene->rack->getModules().empty() || !scene->rack->getCables().empty()))
		throw runtime::Ownership(std::string(what) + " deletes the engine's modules and cables, which Rack's rack "
			"widgets own: clear the rack first (Rack's patch manager does)");
}

}  // namespace

py::object engineModuleHandle(RackModule* module) {
	if (!module)
		return py::none();
	py::object own = engineModuleObject(module);
	if (!own.is_none())
		return own;  // a RackPy module: its own handle
	auto token = runtime::binding().current();
	if (!token)
		return py::none();
	return py::cast(EngineModule{token, module->id});
}

void bindEngine(py::module_ engine) {
	py::class_<EngineModule>(engine, "EngineModule", py::is_final(),
		"A module in Rack's engine that RackPy did not create (another plugin's), found again by its ID at every use: "
		"stale once it leaves the engine.")
		.def_property_readonly("id", [](const EngineModule& m) { return m.id; })
		.def_property_readonly("alive", [](const EngineModule& m) { return m.find() != nullptr; })
		.def_property_readonly("model", [](const EngineModule& m) { return modelObject(m.get().model); })
		.def("is_bypassed", [](const EngineModule& m) { return m.get().isBypassed(); })
		.def("get_num_params", [](const EngineModule& m) { return m.get().getNumParams(); })
		.def("get_num_inputs", [](const EngineModule& m) { return m.get().getNumInputs(); })
		.def("get_num_outputs", [](const EngineModule& m) { return m.get().getNumOutputs(); })
		.def("get_num_lights", [](const EngineModule& m) { return m.get().getNumLights(); })
		.def("__eq__", [](const EngineModule& a, py::handle b) {
			return py::isinstance<EngineModule>(b) && b.cast<const EngineModule&>().id == a.id;
		}, py::is_operator())
		.def("__hash__", [](const EngineModule& m) { return py::hash(py::int_(m.id)); })
		.def("__repr__", [](const EngineModule& m) { return "<EngineModule " + std::to_string(m.id) + ">"; });

	py::class_<EngineHandle>(engine, "Engine", py::is_final(),
		"Rack's engine (Context.engine): modules, cables, parameters and timing. Main thread; calls that take Rack's "
		"engine lock release the GIL.")
		.def_property_readonly("alive", [](const EngineHandle& h) { return h.token->alive() && h.token->context->engine; })
		// Modules
		.def("clear", [](const EngineHandle& h) {
			h.get();
			requireEmptyRack(h, "clear");
			locked(h, "clear", [](Engine& e) { e.clear(); });
		}, "Rack's: removes and deletes every module and cable. Refused while Rack's rack has widgets.")
		.def("get_num_modules", [](const EngineHandle& h) { return h.get().getNumModules(); })
		.def("get_module_ids", [](const EngineHandle& h) {
			return locked(h, "get_module_ids", [](Engine& e) { return e.getModuleIds(); });
		}, "Rack's getModuleIds (both overloads): the IDs of the modules in the engine.")
		.def("add_module", [](const EngineHandle& h, py::object module) {
			Engine& engine = h.get();
			if (!py::isinstance<ModuleRef>(module))
				throw py::type_error("add_module takes a rackpy.raw.engine.Module that Python owns");
			const auto& ref = module.cast<const ModuleRef&>();
			module::Module& m = ref.get();
			if (ref.state->constructing)
				throw runtime::Ownership("a module can go to Rack's engine only after its constructor callback returns");
			if (m.engineOwned.load(std::memory_order_acquire))
				throw runtime::Ownership("Rack's engine already has this module");
			if (!m.model || !m.model->plugin)
				throw runtime::Ownership("Rack saves a module with its model and plugin: use a model of this plugin "
					"(Model.create_module)");
			runtime::refuseEngineReentry("add_module");
			module::Module* released = ref.state->releaseToRack();
			py::gil_scoped_release release;
			runtime::EngineLease lease(*h.token);
			engine.addModule(released);  // Rack assigns an ID if it has none
		}, py::arg("module"),
			"Rack's addModule for a module Python owns: Rack's engine owns it from now on (a module widget may take it "
			"with Model.create_module_widget), and its handles keep working.")
		.def("remove_module", [](const EngineHandle& h, py::object module) {
			Engine& engine = h.get();
			if (!py::isinstance<ModuleRef>(module))
				throw py::type_error("remove_module takes a RackPy module; another plugin's module leaves with its widget");
			const auto& ref = module.cast<const ModuleRef&>();
			module::Module& m = ref.get();
			if (!m.engineOwned.load(std::memory_order_acquire))
				throw runtime::Ownership("Rack's engine does not own this module");
			auto* scene = h.token->context->scene;
			if (scene && scene->rack && scene->rack->getModule(m.id))
				throw runtime::Ownership("this module's widget owns it: delete it with the widget's remove_action");
			if (m.referenced())
				throw runtime::Ownership("controls still point at this module");
			runtime::refuseEngineReentry("remove_module");
			{
				py::gil_scoped_release release;
				runtime::EngineLease lease(*h.token);
				for (int64_t id : engine.getCableIds()) {
					auto* cable = engine.getCable(id);
					if (cable && (cable->inputModule == &m || cable->outputModule == &m))
						throw runtime::Ownership("cables are still connected to this module (Rack removes them first)");
				}
				engine.removeModule(&m);
			}
			ref.state->reclaimFromRack();
		}, py::arg("module"),
			"Rack's removeModule for a RackPy module with no widget, no controls and no cables: Python owns it again.")
		.def("has_module", [](const EngineHandle& h, py::object module) {
			h.get();
			RackModule* m = py::isinstance<EngineModule>(module) ? module.cast<const EngineModule&>().find()
				: &moduleArgument(module);
			return m && locked(h, "has_module", [m](Engine& e) { return e.hasModule(m); });
		}, py::arg("module"))
		.def("get_module", [](const EngineHandle& h, int64_t id) {
			RackModule* m = locked(h, "get_module", [id](Engine& e) { return e.getModule(id); });
			return engineModuleHandle(m);
		}, py::arg("module_id"), "The module with this ID: a RackPy Module, an EngineModule, or None.")
		.def("reset_module", [](const EngineHandle& h, py::object module) {
			RackModule& m = engineModule(h, module);
			locked(h, "reset_module", [&m](Engine& e) { e.resetModule(&m); });
		}, py::arg("module"), "Rack's: a ResetEvent under the exclusive engine lock (no undo step).")
		.def("randomize_module", [](const EngineHandle& h, py::object module) {
			RackModule& m = engineModule(h, module);
			locked(h, "randomize_module", [&m](Engine& e) { e.randomizeModule(&m); });
		}, py::arg("module"))
		.def("bypass_module", [](const EngineHandle& h, py::object module, bool bypassed) {
			RackModule& m = engineModule(h, module);
			locked(h, "bypass_module", [&m, bypassed](Engine& e) { e.bypassModule(&m, bypassed); });
		}, py::arg("module"), py::arg("bypassed"))
		.def("module_to_json", [](const EngineHandle& h, py::object module) {
			RackModule& m = engineModule(h, module);
			json_t* data = locked(h, "module_to_json", [&m](Engine& e) { return e.moduleToJson(&m); });
			std::unique_ptr<json_t, decltype(&json_decref)> held(data, json_decref);
			return runtime::fromJson(data);
		}, py::arg("module"))
		.def("module_from_json", [](const EngineHandle& h, py::object module, py::handle data) {
			RackModule& m = engineModule(h, module);
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			locked(h, "module_from_json", [&](Engine& e) { e.moduleFromJson(&m, json.get()); });
		}, py::arg("module"), py::arg("data"))
		.def("prepare_save_module", [](const EngineHandle& h, py::object module) {
			RackModule& m = engineModule(h, module);
			locked(h, "prepare_save_module", [&m](Engine& e) { e.prepareSaveModule(&m); });
		}, py::arg("module"), "Rack's: a SaveEvent for the module.")
		.def("prepare_save", [](const EngineHandle& h) { locked(h, "prepare_save", [](Engine& e) { e.prepareSave(); }); },
			"Rack's: a SaveEvent for every module, as before Rack saves a patch.")
		// The master module and timing
		.def("set_master_module", [](const EngineHandle& h, py::object module) {
			RackModule* m = module.is_none() ? nullptr : &engineModule(h, module);
			locked(h, "set_master_module", [m](Engine& e) { e.setMasterModule(m); });
		}, py::arg("module"), "Rack's: the module whose audio clock steps the engine, or None for Rack's own thread.")
		.def("get_master_module", [](const EngineHandle& h) { return engineModuleHandle(h.get().getMasterModule()); })
		.def("step_block", [](const EngineHandle& h, int frames) {
			if (frames < 1 || frames > (1 << 16))
				throw std::invalid_argument("frames must be from 1 to 65536");
			// Rack: "Only call this method from the master module." A RackPy master
			// module's code steps the engine; Rack's own thread then waits.
			auto* master = dynamic_cast<module::Module*>(h.get().getMasterModule());
			if (!master)
				throw runtime::Ownership("only the master module steps Rack's engine: make a RackPy module the master "
					"first (set_master_module)");
			locked(h, "step_block", [frames](Engine& e) { e.stepBlock(frames); });
		}, py::arg("frames"),
			"Rack's: advances the engine by frames. Rack allows it only from the master module, so it needs a RackPy "
			"master module. Rack serializes it with any other stepping.")
		.def("get_sample_rate", [](const EngineHandle& h) { return h.get().getSampleRate(); })
		.def("get_sample_time", [](const EngineHandle& h) { return h.get().getSampleTime(); })
		.def("set_suggested_sample_rate", [](const EngineHandle& h, float rate) {
			if (!std::isfinite(rate) || rate < 0.f)
				throw std::invalid_argument("the sample rate must be finite and not negative (0: Rack's fallback)");
			locked(h, "set_suggested_sample_rate", [rate](Engine& e) { e.setSuggestedSampleRate(rate); });
		}, py::arg("suggested_sample_rate"),
			"Rack's: sets the rate unless Rack's settings fix one; 0 gives Rack's fallback, 44100.")
		.def("yield_workers", [](const EngineHandle& h) { h.get().yieldWorkers(); },
			"Rack's hint that the engine's worker threads should block rather than spin.")
		.def("get_frame", [](const EngineHandle& h) { return h.get().getFrame(); })
		.def("get_block", [](const EngineHandle& h) { return h.get().getBlock(); })
		.def("get_block_frame", [](const EngineHandle& h) { return h.get().getBlockFrame(); })
		.def("get_block_time", [](const EngineHandle& h) { return h.get().getBlockTime(); })
		.def("get_block_frames", [](const EngineHandle& h) { return h.get().getBlockFrames(); })
		.def("get_block_duration", [](const EngineHandle& h) { return h.get().getBlockDuration(); })
		.def("get_meter_average", [](const EngineHandle& h) { return h.get().getMeterAverage(); })
		.def("get_meter_max", [](const EngineHandle& h) { return h.get().getMeterMax(); })
		// Params
		.def("set_param_value", [](const EngineHandle& h, py::object module, int paramId, float value) {
			RackModule& m = moduleArgument(module);
			requireParam(m, paramId);
			h.get().setParamValue(&m, paramId, value);
		}, py::arg("module"), py::arg("param_id"), py::arg("value"), "Rack's: the param's raw value, at once.")
		.def("get_param_value", [](const EngineHandle& h, py::object module, int paramId) {
			RackModule& m = moduleArgument(module);
			requireParam(m, paramId);
			return h.get().getParamValue(&m, paramId);
		}, py::arg("module"), py::arg("param_id"))
		.def("set_param_smooth_value", [](const EngineHandle& h, py::object module, int paramId, float value) {
			RackModule& m = engineModule(h, module);  // Rack keeps the smoothed module's raw pointer
			requireParam(m, paramId);
			h.get().setParamSmoothValue(&m, paramId, value);
		}, py::arg("module"), py::arg("param_id"), py::arg("value"),
			"Rack's: moves the param smoothly toward value (as the engine steps). The module must be in the engine.")
		.def("get_param_smooth_value", [](const EngineHandle& h, py::object module, int paramId) {
			RackModule& m = moduleArgument(module);
			requireParam(m, paramId);
			return h.get().getParamSmoothValue(&m, paramId);
		}, py::arg("module"), py::arg("param_id"), "Rack's: the smoothing target, or the value if none.")
		// The engine's JSON
		.def("to_json", [](const EngineHandle& h) {
			json_t* data = locked(h, "to_json", [](Engine& e) { return e.toJson(); });
			std::unique_ptr<json_t, decltype(&json_decref)> held(data, json_decref);
			return runtime::fromJson(data);
		}, "Rack's: the engine's part of a patch (modules, cables, master module).")
		.def("from_json", [](const EngineHandle& h, py::handle data) {
			h.get();
			requireEmptyRack(h, "from_json");
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			locked(h, "from_json", [&](Engine& e) { e.fromJson(json.get()); });
		}, py::arg("data"), "Rack's: clears the engine, then creates the patch's modules and cables. Refused while Rack's "
			"rack has widgets.");
}

py::object engineObject(const std::shared_ptr<runtime::ContextToken>& token) {
	if (!token->context->engine)
		return py::none();
	return runtime::identity(token->context->engine, token->generation, [&] { return py::cast(EngineHandle{token}); });
}

}  // namespace bindings
}  // namespace rackpy
