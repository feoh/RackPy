// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the Engine binding's translation units (engine.cpp, cable.cpp,
// param_handle.cpp): the checked Engine handle, EngineModule, and the rules
// for calls that take Rack's engine lock.
#pragma once
#include <pybind11/pybind11.h>

#include <functional>
#include <memory>
#include <string>

#include <rack.hpp>

#include "module.hpp"
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

using RackModule = rack::engine::Module;

/** Context::engine, checked: the bound context's engine, on its main thread. */
struct EngineHandle {
	std::shared_ptr<runtime::ContextToken> token;
	rack::engine::Engine& get() const {
		runtime::requireMainThread("using Rack's engine");
		if (!token->alive())
			throw runtime::StaleHandle("Rack's context has closed");
		runtime::checkContext(*token);
		if (!token->context->engine)
			throw runtime::StaleHandle("Rack's engine has been deleted");
		return *token->context->engine;
	}
};

/** Deletes a native object Rack's code may need for its destructor (the engine, APP): on the
main thread, outside Rack's engine lock and without the GIL; otherwise on the next UI step. */
void deleteOnMainThread(std::function<void()> deleter);

template <class T>
struct MainThreadDelete {
	void operator()(T* object) const noexcept {
		deleteOnMainThread([object] { delete object; });
	}
};

/** A module RackPy did not create (another plugin's), which Python owns until Engine.add_module. */
struct OwnedForeignModule {
	std::unique_ptr<RackModule, MainThreadDelete<RackModule>> module;
	int64_t id = -1;  // its ID once Rack's engine has it
};

/** A module in Rack's engine that RackPy did not create: found again by its ID at every use. Or one
Model.create_module made, which Python owns until Engine.add_module gives it to Rack's engine. */
struct EngineModule {
	std::shared_ptr<runtime::ContextToken> token;
	int64_t id;
	std::shared_ptr<OwnedForeignModule> owned;

	bool pythonOwned() const { return owned && owned->module; }
	int64_t currentId() const {
		if (!owned)
			return id;
		return owned->module ? owned->module->id : owned->id;
	}
	RackModule* find() const {
		runtime::requireMainThread("using an engine module");
		if (pythonOwned())
			return owned->module.get();
		int64_t key = currentId();
		if (key < 0 || !token->alive() || !token->context->engine)
			return nullptr;
		runtime::refuseEngineReentry("finding an engine module");
		pybind11::gil_scoped_release release;
		runtime::EngineLease lease(*token);
		return token->context->engine->getModule(key);
	}
	RackModule& get() const {
		RackModule* m = find();
		if (!m)
			throw runtime::StaleHandle("module " + std::to_string(currentId()) + " is no longer in Rack's engine");
		return *m;
	}
};

/** A Python module argument: a RackPy Module handle or an EngineModule. */
inline RackModule& moduleArgument(pybind11::handle value) {
	if (pybind11::isinstance<ModuleRef>(value))
		return value.cast<const ModuleRef&>().get();
	if (pybind11::isinstance<EngineModule>(value))
		return value.cast<const EngineModule&>().get();
	throw pybind11::type_error("expected a rackpy.raw.engine.Module or EngineModule");
}

/** Runs a locking engine call: refused inside an exclusive engine callback, then without the GIL. */
template <class F>
auto locked(const EngineHandle& h, const char* what, F f) {
	rack::engine::Engine& engine = h.get();
	runtime::refuseEngineReentry(what);
	pybind11::gil_scoped_release release;
	runtime::EngineLease lease(*h.token);
	return f(engine);
}

/** A module argument that must be in Rack's engine. */
inline RackModule& engineModule(const EngineHandle& h, pybind11::handle value) {
	RackModule& m = moduleArgument(value);
	if (!locked(h, "checking a module", [&m](rack::engine::Engine& e) { return e.hasModule(&m); }))
		throw runtime::Ownership("the module is not in Rack's engine");
	return m;
}

inline void requireParam(RackModule& m, int paramId) {
	if (paramId < 0 || static_cast<size_t>(paramId) >= m.params.size())
		throw std::invalid_argument("param ID " + std::to_string(paramId) + " is out of range for this module (" +
			std::to_string(m.params.size()) + ")");
}

/** Runs `f` while Rack's engine holds its lock, exclusively or shared, through Rack's public
calls that lock and then call a module (resetModule, prepareSaveModule) on a private module that
is not in the engine. For Rack's calls that need the caller to hold the lock. Native code only:
no Python runs while the lock is held. Main thread, GIL released by the caller. */
void underEngineLock(rack::engine::Engine& engine, bool exclusive, const std::function<void()>& f);

/** Rack's _NoLock calls need the caller to hold the engine lock: Python does only in a callback
Rack runs under the exclusive lock. */
inline void requireLockHeld(const char* what) {
	if (!runtime::insideExclusive())
		throw runtime::EngineLocked(std::string(what) + " needs Rack's engine lock held, which Python has only inside "
			"a callback Rack runs under the exclusive lock (a ResetEvent, say); call the locking method instead");
}

void bindCables(pybind11::module_ engine, pybind11::class_<EngineHandle>& cls);
void bindParamHandles(pybind11::module_ engine, pybind11::class_<EngineHandle>& cls);

}  // namespace bindings
}  // namespace rackpy
