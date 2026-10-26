// SPDX-License-Identifier: GPL-3.0-or-later
// rack::engine::Cable -> rackpy.raw.engine.Cable, and the Engine's cable
// operations (RP-24B).
//
// A Cable that Python made owns Rack's struct and holds its endpoints as
// module handles, never Rack's pointers; Rack's pointers are set when the
// engine takes it. A Cable in Rack's engine (added from Python, or found with
// get_cable) is found again by its ID at every use, since Rack deletes cables
// it owns without telling anyone: a cable widget deletes its own, and
// Engine::clear the rest.
//
// Rack asserts, when it deletes a module, that no cable in the engine still
// touches it, and a module widget deletes its module. So a cable Python adds
// (which has no cable widget to remove it first) may only join RackPy modules
// that no module widget owns: modules Python put into the engine itself.
// Cables with widgets are RackWidget's (RP-28E).
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../module/module.hpp"
#include "../app/app.hpp"
#include "engine.hpp"
#include "engine_internal.hpp"
#include "rackpy/runtime/json.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
using rack::engine::Cable;
using rack::engine::Engine;

namespace {

using JsonPtr = std::unique_ptr<json_t, decltype(&json_decref)>;

struct CableHandle {
	// Python's cable: Rack's struct, and the endpoints as module handles.
	std::unique_ptr<Cable> owned;
	py::object input = py::none();
	py::object output = py::none();
	// Rack's engine's cable: its ID and context.
	std::shared_ptr<runtime::ContextToken> token;
	int64_t id = -1;

	CableHandle() : owned(std::make_unique<Cable>()) {}
	CableHandle(std::shared_ptr<runtime::ContextToken> token, int64_t id) : token(std::move(token)), id(id) {}

	bool inEngine() const { return !owned; }
	Cable* find() const {
		runtime::requireMainThread("using an engine cable");
		if (!token->alive() || !token->context->engine)
			return nullptr;
		runtime::refuseEngineReentry("finding an engine cable");
		py::gil_scoped_release release;
		runtime::EngineLease lease(*token);
		return token->context->engine->getCable(id);
	}
	Cable& engineCable() const {
		Cable* c = find();
		if (!c)
			throw runtime::StaleHandle("cable " + std::to_string(id) + " is no longer in Rack's engine");
		return *c;
	}
	Cable& own(const char* what) {
		runtime::requireMainThread("using a cable");
		if (!owned)
			throw runtime::Ownership(std::string("Rack's engine has this cable, so ") + what + " is fixed");
		return *owned;
	}
};

void requireModuleHandle(py::handle value) {
	if (!value.is_none() && !py::isinstance<ModuleRef>(value) && !py::isinstance<EngineModule>(value))
		throw py::type_error("a cable's module is a rackpy.raw.engine.Module, an EngineModule or None");
}

/** Python's cable with Rack's pointers set from its handles, for Rack's calls that read them. */
Cable& withPointers(CableHandle& h) {
	if (h.input.is_none() || h.output.is_none())
		throw std::invalid_argument("the cable needs both its input and output modules");
	h.owned->inputModule = &moduleArgument(h.input);
	h.owned->outputModule = &moduleArgument(h.output);
	return *h.owned;
}

void clearPointers(CableHandle& h) {
	h.owned->inputModule = nullptr;
	h.owned->outputModule = nullptr;
}

/** A cable endpoint Python may use: a RackPy module in Rack's engine that no module widget owns. */
module::Module& widgetlessEndpoint(const EngineHandle& e, py::handle value, const char* side) {
	if (!py::isinstance<ModuleRef>(value))
		throw runtime::Ownership(std::string("the cable's ") + side + " module is another plugin's, which a module widget "
			"owns; Rack would assert when deleting it with this cable in the engine (cables with widgets: RackWidget)");
	module::Module& m = value.cast<const ModuleRef&>().get();
	if (!m.engineOwned.load(std::memory_order_acquire) || !locked(e, "add_cable", [&m](Engine& en) { return en.hasModule(&m); }))
		throw runtime::Ownership(std::string("the cable's ") + side + " module is not in Rack's engine");
	if (widgets::moduleHasWidget(&m))
		throw runtime::Ownership(std::string("the cable's ") + side + " module has a module widget, which deletes it; Rack "
			"would assert with this cable in the engine (cables with widgets: RackWidget)");
	return m;
}

py::object cableObject(const std::shared_ptr<runtime::ContextToken>& token, int64_t id) {
	return py::cast(CableHandle(token, id));
}

}  // namespace

bool engineCablesTouch(RackModule& module) {
	auto token = runtime::binding().current();
	if (!token || !token->context->engine)
		return false;
	runtime::refuseEngineReentry("checking a module's cables");
	py::gil_scoped_release release;
	runtime::EngineLease lease(*token);
	Engine& engine = *token->context->engine;
	for (int64_t id : engine.getCableIds()) {
		Cable* c = engine.getCable(id);
		if (c && (c->inputModule == &module || c->outputModule == &module))
			return true;
	}
	return false;
}

rack::engine::Cable* engineCableForWidget(py::handle value, const std::function<void(const Cable&)>& check) {
	runtime::requireMainThread("giving a cable to a cable widget");
	if (!py::isinstance<CableHandle>(value))
		throw py::type_error("expected a rackpy.raw.engine.Cable");
	auto& c = value.cast<CableHandle&>();
	if (c.inEngine())
		throw runtime::Ownership("Rack's engine already has this cable; a cable widget adopts a cable Python made");
	Cable& cable = withPointers(c);
	if (cable.inputId < 0 || static_cast<size_t>(cable.inputId) >= cable.inputModule->inputs.size() ||
		cable.outputId < 0 || static_cast<size_t>(cable.outputId) >= cable.outputModule->outputs.size()) {
		clearPointers(c);
		throw std::invalid_argument("the cable's port IDs are out of range");
	}
	try {
		check(cable);  // the widget's own requirements (module widgets, port widgets)
	}
	catch (...) {
		clearPointers(c);
		throw;
	}
	auto token = runtime::binding().current();
	if (!token || !token->context->engine) {
		clearPointers(c);
		throw runtime::WrongContext("a cable goes into Rack's engine only in its live context");
	}
	runtime::refuseEngineReentry("CableWidget.set_cable");
	Cable* raw = c.owned.get();
	{
		py::gil_scoped_release release;
		runtime::EngineLease lease(*token);
		token->context->engine->addCable(raw);  // Rack's setCable needs it in the engine
	}
	c.owned.release();
	c.id = raw->id;
	c.token = token;
	c.input = py::none();
	c.output = py::none();
	return raw;
}

void undoEngineCableForWidget(py::handle value, rack::engine::Cable* raw) {
	auto& c = value.cast<CableHandle&>();
	py::object input = engineModuleHandle(raw->inputModule);
	py::object output = engineModuleHandle(raw->outputModule);
	{
		py::gil_scoped_release release;
		runtime::EngineLease lease(*c.token);
		c.token->context->engine->removeCable(raw);
	}
	c.owned.reset(raw);
	c.owned->inputModule = c.owned->outputModule = nullptr;
	c.input = input;
	c.output = output;
	c.token.reset();
	c.id = -1;
}

py::object pythonOwnedCable(rack::engine::Cable* removed) {
	CableHandle handle;
	handle.input = engineModuleHandle(removed->inputModule);
	handle.output = engineModuleHandle(removed->outputModule);
	handle.owned.reset(removed);  // Rack's `new`
	handle.owned->inputModule = handle.owned->outputModule = nullptr;
	return py::cast(std::move(handle));
}

py::object engineCableHandle(int64_t id) {
	auto token = runtime::binding().current();
	if (!token)
		return py::none();
	return cableObject(token, id);
}

void bindCables(py::module_ engine, py::class_<EngineHandle>& cls) {
	py::class_<CableHandle>(engine, "Cable", py::is_final(),
		"Rack's Cable: an output connected to an input. One Python makes holds module handles until Rack's engine takes "
		"it; one in Rack's engine is found again by its ID at every use.")
		.def(py::init<>())
		.def_property_readonly("in_engine", &CableHandle::inEngine,
			"True once Rack's engine has the cable (it may since have deleted it: see alive).")
		.def_property_readonly("alive", [](const CableHandle& h) { return !h.inEngine() || h.find() != nullptr; })
		.def_property("id", [](const CableHandle& h) { return h.inEngine() ? h.id : h.owned->id; },
			[](CableHandle& h, int64_t id) {
				if (id < -1 || id >= (int64_t(1) << 53))
					throw std::invalid_argument("a cable ID is -1 (unset) or from 0 to 2**53 - 1");
				h.own("its ID").id = id;
			}, "Rack's: unique in the engine, -1 until the engine assigns one.")
		.def_property("input_module", [](const CableHandle& h) {
				return h.inEngine() ? engineModuleHandle(h.engineCable().inputModule) : h.input;
			}, [](CableHandle& h, py::object m) {
				requireModuleHandle(m);
				h.own("its input module");
				h.input = m;
			})
		.def_property("input_id", [](const CableHandle& h) { return h.inEngine() ? h.engineCable().inputId : h.owned->inputId; },
			[](CableHandle& h, int id) { h.own("its input ID").inputId = id; })
		.def_property("output_module", [](const CableHandle& h) {
				return h.inEngine() ? engineModuleHandle(h.engineCable().outputModule) : h.output;
			}, [](CableHandle& h, py::object m) {
				requireModuleHandle(m);
				h.own("its output module");
				h.output = m;
			})
		.def_property("output_id", [](const CableHandle& h) { return h.inEngine() ? h.engineCable().outputId : h.owned->outputId; },
			[](CableHandle& h, int id) { h.own("its output ID").outputId = id; })
		.def("to_json", [](CableHandle& h) {
			runtime::requireMainThread("saving a cable");
			json_t* data;
			if (h.inEngine()) {
				Cable& c = h.engineCable();
				data = c.toJson();
			}
			else {
				Cable& c = withPointers(h);
				data = c.toJson();
				clearPointers(h);
			}
			JsonPtr held(data, json_decref);
			return runtime::fromJson(data);
		}, "Rack's: the cable's patch JSON (its ID, modules' IDs and port IDs).")
		.def("from_json", [](CableHandle& h, py::handle data) {
			Cable& c = h.own("its JSON");
			auto context = runtime::binding().current();
			if (!context || rack::contextGet() != context->context || !context->context->engine)
				throw runtime::WrongContext("Cable.from_json finds its modules in Rack's engine: it needs Rack's live bound UI "
					"context");
			runtime::refuseEngineReentry("Cable.from_json");
			JsonPtr json(runtime::toJson(data), json_decref);
			Cable loaded = c;
			std::optional<std::string> error;
			{
				py::gil_scoped_release release;
				runtime::EngineLease lease(*context);
				// Rack's Cable::fromJson finds the modules with getModule_NoLock: hold the lock for it.
				underEngineLock(*context->context->engine, false, [&] {
					try {
						loaded.fromJson(json.get());
					}
					catch (const std::exception& e) {
						error = e.what();
					}
				});
			}
			if (error)
				throw std::invalid_argument(*error);
			py::object input = engineModuleHandle(loaded.inputModule);
			py::object output = engineModuleHandle(loaded.outputModule);
			c = loaded;
			h.input = input;
			h.output = output;
			clearPointers(h);
		}, py::arg("data"),
			"Rack's: sets the cable from patch JSON, finding its modules in Rack's engine by ID (the ID only if unset).")
		.def("__eq__", [](const CableHandle& a, py::handle b) {
			if (!py::isinstance<CableHandle>(b))
				return false;
			const auto& o = b.cast<const CableHandle&>();
			return a.inEngine() ? o.inEngine() && o.id == a.id : &a == &o;
		}, py::is_operator())
		.def("__hash__", [](py::handle self) {
			const auto& h = self.cast<const CableHandle&>();
			return h.inEngine() ? py::hash(py::int_(h.id)) : py::hash(py::int_(reinterpret_cast<uintptr_t>(self.ptr())));
		})
		.def("__repr__", [](const CableHandle& h) {
			return h.inEngine() ? "<Cable " + std::to_string(h.id) + " in Rack's engine>" : std::string("<Cable>");
		});

	cls
		.def("get_num_cables", [](const EngineHandle& h) { return h.get().getNumCables(); })
		.def("get_cable_ids", [](const EngineHandle& h) {
			return locked(h, "get_cable_ids", [](Engine& e) { return e.getCableIds(); });
		}, "Rack's getCableIds (both overloads): the IDs of the cables in the engine.")
		.def("get_cable", [](const EngineHandle& h, int64_t id) -> py::object {
			Cable* c = locked(h, "get_cable", [id](Engine& e) { return e.getCable(id); });
			return c ? cableObject(h.token, id) : py::none();
		}, py::arg("cable_id"), "The cable with this ID (Rack's, a cable widget's, or one Python added), or None.")
		.def("has_cable", [](const EngineHandle& h, const CableHandle& c) {
			h.get();
			if (!c.inEngine())
				return false;  // Rack's engine has only cables it took
			return c.token == h.token && c.find() != nullptr;
		}, py::arg("cable"))
		.def("add_cable", [](const EngineHandle& h, CableHandle& c) {
			h.get();
			if (c.inEngine())
				throw runtime::Ownership("Rack's engine already has this cable");
			if (c.input.is_none() || c.output.is_none())
				throw std::invalid_argument("the cable needs both its input and output modules");
			module::Module& in = widgetlessEndpoint(h, c.input, "input");
			module::Module& out = widgetlessEndpoint(h, c.output, "output");
			if (c.owned->inputId < 0 || static_cast<size_t>(c.owned->inputId) >= in.inputs.size())
				throw std::invalid_argument("input ID " + std::to_string(c.owned->inputId) + " is out of range");
			if (c.owned->outputId < 0 || static_cast<size_t>(c.owned->outputId) >= out.outputs.size())
				throw std::invalid_argument("output ID " + std::to_string(c.owned->outputId) + " is out of range");
			c.owned->inputModule = &in;
			c.owned->outputModule = &out;
			Cable* cable = c.owned.get();
			locked(h, "add_cable", [cable](Engine& e) { e.addCable(cable); });  // Rack assigns an ID if needed
			c.owned.release();
			c.id = cable->id;
			c.token = h.token;
			c.input = py::none();
			c.output = py::none();
		}, py::arg("cable"),
			"Rack's addCable for a cable Python made, between RackPy modules that Python put into the engine (no module "
			"widget owns them). Rack's engine owns it from then on; the handle finds it by ID.")
		.def("remove_cable", [](const EngineHandle& h, CableHandle& c) {
			h.get();
			if (!c.inEngine())
				throw runtime::Ownership("Rack's engine does not have this cable");
			Cable& cable = c.engineCable();
			auto* scene = h.token->context->scene;
			if (scene && scene->rack && scene->rack->getCable(c.id))
				throw runtime::Ownership("a cable widget owns this cable: remove it with Rack's rack (RackWidget)");
			py::object input = engineModuleHandle(cable.inputModule);
			py::object output = engineModuleHandle(cable.outputModule);
			Cable* raw = &cable;
			locked(h, "remove_cable", [raw](Engine& e) { e.removeCable(raw); });
			c.owned.reset(raw);  // Rack's `new`, as CableWidget's: Python owns it now
			c.owned->inputModule = nullptr;
			c.owned->outputModule = nullptr;
			c.input = input;
			c.output = output;
			c.token.reset();
			c.id = -1;
		}, py::arg("cable"),
			"Rack's removeCable for a cable no cable widget owns: Python owns it again. Other handles to it go stale.");
}

}  // namespace bindings
}  // namespace rackpy
