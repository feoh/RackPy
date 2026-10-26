// SPDX-License-Identifier: GPL-3.0-or-later
// rack::engine::ParamHandle -> rackpy.raw.engine.ParamHandle, and the Engine's
// parameter-handle operations (RP-24B).
//
// Rack's engine keeps raw pointers to the handles added to it and never
// deletes them; their owners (normally modules) remove them before freeing
// them. RackPy owns the handles Python makes in native slots, which stay
// allocated while the engine has them, whatever happens to the Python object.
//
// Rack's Engine::clear (a new or loaded patch, or the engine's destruction)
// removes every handle without telling its owner, and removing a handle the
// engine no longer has fails Rack's assertion; Rack has no call that asks.
// So RackPy keeps its own sentinel handle in the engine, mapped to a module
// ID Rack never assigns: when Rack's lookup no longer finds it, the engine
// was cleared, and RackPy's handles went with it. Python's handles still in
// the engine when the context closes are removed then, while the engine lives.
#include <pybind11/pybind11.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../support/overrides.hpp"
#include "engine.hpp"
#include "engine_internal.hpp"
#include "rackpy/runtime/interpreter.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
using rack::engine::Engine;
using rack::engine::ParamHandle;

namespace {

/** Rack assigns module IDs below 2**53 (they are saved as JSON numbers). */
constexpr int64_t kSentinelModuleId = int64_t(1) << 53;
constexpr int kSentinelParamId = -1;

struct Slot {
	ParamHandle handle{};
	bool registered = false;
	uint64_t generation = 0;
	Slot() { handle.color = nvgRGBf(0.f, 0.f, 0.f); }
};

/** Main thread only. */
struct Registry {
	ParamHandle sentinel{};
	const Engine* engine = nullptr;  // the engine the sentinel was added to
	uint64_t generation = 0;         // bumped at each (re)install: older registrations are gone
	std::vector<std::shared_ptr<Slot>> slots;  // Python's handles in the engine
	bool cleanupRegistered = false;
};

Registry& registry() {
	static Registry r;
	return r;
}

/** With the engine lock held, or inside a lookup that takes it: is the sentinel still in `engine`? */
bool sentinelPresent(Engine& engine, bool lockHeld) {
	auto& r = registry();
	if (r.engine != &engine)
		return false;
	ParamHandle* found = lockHeld ? engine.getParamHandle_NoLock(kSentinelModuleId, kSentinelParamId)
		: engine.getParamHandle(kSentinelModuleId, kSentinelParamId);
	return found == &r.sentinel;
}

/** Forgets Python's registrations if Rack cleared the engine since. GIL released or not: native only. */
void sync(Engine& engine, bool lockHeld) {
	auto& r = registry();
	if (r.engine && !sentinelPresent(engine, lockHeld)) {
		for (auto& slot : r.slots)
			slot->registered = false;
		r.slots.clear();
		r.engine = nullptr;
	}
}

/** Adds the sentinel if the engine lacks it. Not with the lock held: addParamHandle locks. */
void ensureSentinel(Engine& engine) {
	auto& r = registry();
	sync(engine, false);
	if (r.engine)
		return;
	r.sentinel.moduleId = -1;  // Rack's addParamHandle needs a blank handle; clear() left the old IDs
	r.sentinel.module = nullptr;
	engine.addParamHandle(&r.sentinel);
	engine.updateParamHandle(&r.sentinel, kSentinelModuleId, kSentinelParamId, false);
	r.engine = &engine;
	++r.generation;
}

bool registered(const Slot& slot) {
	return slot.registered && slot.generation == registry().generation && registry().engine;
}

void removeAllAtClose(const std::shared_ptr<runtime::ContextToken>& token) noexcept {
	auto& r = registry();
	if (!token->context->engine || r.slots.empty())
		return;
	Engine& engine = *token->context->engine;
	try {
		sync(engine, false);
		for (auto& slot : r.slots) {
			engine.removeParamHandle(&slot->handle);
			slot->registered = false;
		}
		r.slots.clear();
	}
	catch (...) {
	}
}

/** A ParamHandle from Python: one Python made (its slot), or another owner's, found by its IDs. */
struct ParamHandleRef {
	std::shared_ptr<Slot> slot;
	// Another owner's handle (a module's): found again with getParamHandle at every use.
	std::shared_ptr<runtime::ContextToken> token;
	int64_t moduleId = -1;
	int paramId = 0;

	bool ours() const { return slot != nullptr; }
	ParamHandle* findForeign(bool lockHeld) const {
		runtime::requireMainThread("using a parameter handle");
		if (!token->alive() || !token->context->engine)
			return nullptr;
		Engine& engine = *token->context->engine;
		if (lockHeld)
			return engine.getParamHandle_NoLock(moduleId, paramId);
		runtime::refuseEngineReentry("finding a parameter handle");
		py::gil_scoped_release release;
		runtime::EngineLease lease(*token);
		return engine.getParamHandle(moduleId, paramId);
	}
	ParamHandle& get(bool lockHeld = false) const {
		runtime::requireMainThread("using a parameter handle");
		if (slot)
			return slot->handle;
		ParamHandle* found = findForeign(lockHeld);
		if (!found)
			throw runtime::StaleHandle("no parameter handle is mapped to module " + std::to_string(moduleId) + ", param " +
				std::to_string(paramId) + " any more");
		return *found;
	}
};

/** The Python object for a handle Rack's lookup returned. */
py::object handleObject(const std::shared_ptr<runtime::ContextToken>& token, ParamHandle* found) {
	if (!found || found == &registry().sentinel)
		return py::none();
	for (auto& slot : registry().slots)
		if (&slot->handle == found)
			return py::cast(ParamHandleRef{slot, nullptr, -1, 0});
	return py::cast(ParamHandleRef{nullptr, token, found->moduleId, found->paramId});
}

int64_t moduleIdArgument(py::handle value) {
	if (py::isinstance<py::int_>(value))
		return value.cast<int64_t>();
	deprecated("Engine.get_param_handle(module, param_id) is deprecated in Rack; pass the module's ID");
	return moduleArgument(value).id;
}

/** Rack's update rules, checked: Rack indexes the param unchecked once the module exists. */
void checkUpdate(Engine& engine, int64_t moduleId, int paramId, bool lockHeld) {
	if (moduleId == kSentinelModuleId)
		throw std::invalid_argument("module IDs are below 2**53");
	if (moduleId < 0)
		return;  // unmaps the handle
	if (paramId < 0)
		throw std::invalid_argument("param IDs are not negative");
	RackModule* m = lockHeld ? engine.getModule_NoLock(moduleId) : engine.getModule(moduleId);
	if (m && static_cast<size_t>(paramId) >= m->params.size())
		throw std::invalid_argument("param ID " + std::to_string(paramId) + " is out of range for module " +
			std::to_string(moduleId));
}

/** The handle to update: one of Python's in the engine, or another owner's. */
ParamHandle& updatable(Engine& engine, ParamHandleRef& ref, bool lockHeld) {
	if (ref.ours()) {
		sync(engine, lockHeld);
		if (!registered(*ref.slot))
			throw runtime::Ownership("Rack's engine does not have this parameter handle (add_param_handle first)");
		return ref.slot->handle;
	}
	return ref.get(lockHeld);
}

void afterUpdate(ParamHandleRef& ref, ParamHandle& handle) {
	if (!ref.ours()) {  // the view follows the handle to its new IDs
		ref.moduleId = handle.moduleId;
		ref.paramId = handle.paramId;
	}
}

}  // namespace

void bindParamHandles(py::module_ engine, py::class_<EngineHandle>& cls) {
	py::class_<ParamHandleRef>(engine, "ParamHandle", py::is_final(),
		"Rack's ParamHandle: a weak link to one module's param, managed by Rack's engine. One Python makes is RackPy's "
		"until Rack's engine has it; another owner's (a module's) is found again by its IDs at every use.")
		.def(py::init([] { return ParamHandleRef{std::make_shared<Slot>(), nullptr, -1, 0}; }))
		.def_property_readonly("ours", &ParamHandleRef::ours, "True for a handle Python made.")
		.def_property_readonly("in_engine", [](const ParamHandleRef& ref) {
			if (!ref.ours())
				return ref.findForeign(false) != nullptr;
			auto token = runtime::binding().current();
			if (!token || !token->context->engine)
				return false;
			runtime::refuseEngineReentry("checking a parameter handle");
			Engine& e = *token->context->engine;
			{
				py::gil_scoped_release release;
				runtime::EngineLease lease(*token);
				sync(e, false);
			}
			return registered(*ref.slot);
		})
		.def_property("module_id", [](const ParamHandleRef& ref) { return ref.get().moduleId; },
			[](ParamHandleRef& ref, int64_t id) {
				if (!ref.ours() || registered(*ref.slot))
					throw runtime::Ownership("Rack: set a handle's IDs with Engine.update_param_handle");
				ref.slot->handle.moduleId = id;
			}, "Rack's: the module's ID, or -1. Rack's engine sets it (update_param_handle).")
		.def_property("param_id", [](const ParamHandleRef& ref) { return ref.get().paramId; },
			[](ParamHandleRef& ref, int id) {
				if (!ref.ours() || registered(*ref.slot))
					throw runtime::Ownership("Rack: set a handle's IDs with Engine.update_param_handle");
				ref.slot->handle.paramId = id;
			})
		.def_property_readonly("module", [](const ParamHandleRef& ref) { return engineModuleHandle(ref.get().module); },
			"Rack's: the module once it is in the engine, else None (a RackPy Module or an EngineModule).")
		.def_property("text", [](const ParamHandleRef& ref) { return ref.get().text; },
			[](ParamHandleRef& ref, const std::string& text) {
				if (!ref.ours())
					throw runtime::Ownership("another owner's parameter handle is read-only");
				ref.slot->handle.text = text;
			}, "Rack's: the label Rack shows for the mapping.")
		.def_property("color", [](const ParamHandleRef& ref) { return ref.get().color; },
			[](ParamHandleRef& ref, NVGcolor color) {
				if (!ref.ours())
					throw runtime::Ownership("another owner's parameter handle is read-only");
				ref.slot->handle.color = color;
			}, "Rack's: the colour of the mapping's indicator on the param's widget.")
		.def("__eq__", [](const ParamHandleRef& a, py::handle b) {
			if (!py::isinstance<ParamHandleRef>(b))
				return false;
			const auto& o = b.cast<const ParamHandleRef&>();
			return a.ours() ? a.slot == o.slot : !o.ours() && a.moduleId == o.moduleId && a.paramId == o.paramId;
		}, py::is_operator())
		.def("__hash__", [](const ParamHandleRef& ref) {
			return ref.ours() ? py::hash(py::int_(reinterpret_cast<uintptr_t>(ref.slot.get())))
				: py::hash(py::make_tuple(ref.moduleId, ref.paramId));
		});

	cls
		.def("add_param_handle", [](const EngineHandle& h, ParamHandleRef& ref) {
			h.get();
			if (!ref.ours())
				throw runtime::Ownership("another owner's parameter handle is already in Rack's engine");
			if (ref.slot->handle.moduleId >= 0)
				throw std::invalid_argument("Rack adds only blank handles: module_id must be -1 (then update_param_handle)");
			auto slot = ref.slot;
			bool added = locked(h, "add_param_handle", [&slot](Engine& e) {
				ensureSentinel(e);
				if (registered(*slot))
					return false;
				e.addParamHandle(&slot->handle);
				return true;
			});
			if (!added)
				throw runtime::Ownership("Rack's engine already has this parameter handle");
			slot->registered = true;
			slot->generation = registry().generation;
			registry().slots.push_back(slot);
			if (!registry().cleanupRegistered) {
				registry().cleanupRegistered = true;
				auto token = h.token;
				runtime::onContextClosing([token] { removeAllAtClose(token); });
			}
		}, py::arg("param_handle"),
			"Rack's addParamHandle for a blank handle Python made. RackPy keeps it allocated while the engine has it; "
			"Rack's Engine.clear (a new or loaded patch) removes it.")
		.def("remove_param_handle", [](const EngineHandle& h, ParamHandleRef& ref) {
			h.get();
			if (!ref.ours())
				throw runtime::Ownership("another owner removes its own parameter handle");
			auto slot = ref.slot;
			bool removed = locked(h, "remove_param_handle", [&slot](Engine& e) {
				sync(e, false);
				if (!registered(*slot))
					return false;
				e.removeParamHandle(&slot->handle);
				return true;
			});
			if (!removed)
				throw runtime::Ownership("Rack's engine does not have this parameter handle");
			slot->registered = false;
			auto& slots = registry().slots;
			slots.erase(std::remove(slots.begin(), slots.end(), slot), slots.end());
		}, py::arg("param_handle"), "Rack's removeParamHandle for a handle Python added.")
		.def("get_param_handle", [](const EngineHandle& h, py::object module, int paramId) {
			int64_t moduleId = moduleIdArgument(module);
			ParamHandle* found = locked(h, "get_param_handle", [&](Engine& e) {
				sync(e, false);
				return e.getParamHandle(moduleId, paramId);
			});
			return handleObject(h.token, found);
		}, py::arg("module_id"), py::arg("param_id"),
			"Rack's getParamHandle: the handle mapped to this param, or None. Rack's deprecated overload takes the "
			"module itself.")
		.def("get_param_handle_no_lock", [](const EngineHandle& h, int64_t moduleId, int paramId) {
			Engine& e = h.get();
			requireLockHeld("get_param_handle_no_lock");
			sync(e, true);
			return handleObject(h.token, e.getParamHandle_NoLock(moduleId, paramId));
		}, py::arg("module_id"), py::arg("param_id"),
			"Rack's getParamHandle_NoLock: only inside a callback Rack runs under the exclusive lock.")
		.def("update_param_handle", [](const EngineHandle& h, ParamHandleRef& ref, int64_t moduleId, int paramId,
			bool overwrite) {
			h.get();
			if (!ref.ours())
				ref.get();  // finds it, or raises StaleHandleError
			ParamHandle* handle = locked(h, "update_param_handle", [&](Engine& e) {
				checkUpdate(e, moduleId, paramId, false);
				ParamHandle& target = updatable(e, ref, false);
				e.updateParamHandle(&target, moduleId, paramId, overwrite);
				return &target;
			});
			afterUpdate(ref, *handle);
		}, py::arg("param_handle"), py::arg("module_id"), py::arg("param_id"), py::arg("overwrite") = true,
			"Rack's: maps the handle to a param (module_id -1 unmaps it). With overwrite, a handle already on that param "
			"is unmapped; without, this one is unmapped instead.")
		.def("update_param_handle_no_lock", [](const EngineHandle& h, ParamHandleRef& ref, int64_t moduleId, int paramId,
			bool overwrite) {
			Engine& e = h.get();
			requireLockHeld("update_param_handle_no_lock");
			checkUpdate(e, moduleId, paramId, true);
			ParamHandle& target = updatable(e, ref, true);
			e.updateParamHandle_NoLock(&target, moduleId, paramId, overwrite);
			afterUpdate(ref, target);
		}, py::arg("param_handle"), py::arg("module_id"), py::arg("param_id"), py::arg("overwrite") = true,
			"Rack's updateParamHandle_NoLock: only inside a callback Rack runs under the exclusive lock.");
}

}  // namespace bindings
}  // namespace rackpy
