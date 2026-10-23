// SPDX-License-Identifier: GPL-3.0-or-later
// Python-created PortInfo and LightInfo: standalone objects with weak module
// links (RP-14B2b2), and objects a module slot owns after a class factory
// transferred them (RP-14B2b5).
#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/trampoline_self_life_support.h>

#include <memory>

#include "module.hpp"
#include "../support/overrides.hpp"
#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;

/** Python overrides of the info classes' virtual names (callback strategy "ui").

Standalone, Python owns the object and only `module` is main-thread-only.
Transferred, the module slot owns the native object and keeps its Python half
alive; Rack-owned metadata is used on the main thread only, like a module's
component views. Retired, the slot has handed it back: the Python object
survives while referenced, but every bound access raises StaleHandle. */
template <typename Base>
struct InfoTrampoline : Base, module::TransferredMetadata, py::trampoline_self_life_support {
	using Base::Base;
	std::weak_ptr<module::Construction> moduleState;
	std::shared_ptr<module::ModuleBackreference> moduleLink;
	bool inSlot = false;
	bool retired = false;

	~InfoTrampoline() override {
		if (moduleLink)
			moduleLink->detach();
	}
	bool transferred() const override { return inSlot; }
	void check() const {
		if (!inSlot && !retired)
			return;
		runtime::requireMainThread("using metadata that a module owns");
		if (retired)
			throw runtime::StaleHandle("this metadata was replaced or its module was deleted");
	}
	void detachLink() {
		if (moduleLink)
			moduleLink->detach();
		moduleLink.reset();
	}
	/** Hand native ownership back to the Python object (pybind11 reclaim).
	Runs on the thread deleting the module or replacing the slot, normally the
	main thread with the GIL already held. Finalization is refused while any
	transferred object lives, so the interpreter is alive here. */
	void retire() noexcept override {
		if (!runtime::interpreterAlive()) {
			// Unreachable by ADR 007: leak rather than touch a dead interpreter.
			runtime::diagnose(runtime::DiagnosticCode::ShutdownLeak, "transferred metadata outlived Python");
			return;
		}
		PyGILState_STATE gil = PyGILState_Ensure();
		inSlot = false;
		retired = true;
		this->module = nullptr;
		moduleState.reset();
		detachLink();
		runtime::noteTransferredDeleted();
		try {
			// The slot's ownership returns to the existing Python object. Its
			// last reference may go here and run Python finalizers: the slot is
			// already empty and this object is already retired.
			py::object self = py::cast(std::unique_ptr<Base>(this));
		}
		catch (...) {
			// A failed reclaim leaves the unique_ptr owning, which deleted the
			// object; pybind11's self-life support released the Python half.
			runtime::diagnose(runtime::DiagnosticCode::CallbackRaised, "reclaiming transferred metadata failed");
		}
		PyGILState_Release(gil);
	}
	std::string getName() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_name", [this] { return Base::getName(); });
	}
	std::string getDescription() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_description", [this] { return Base::getDescription(); });
	}
};

/** Every Python-visible PortInfo/LightInfo is the alias (py::init_alias). */
template <typename Info>
InfoTrampoline<Info>& infoAlias(Info& info) {
	return dynamic_cast<InfoTrampoline<Info>&>(info);
}

/** The existing Python object of a transferred slot, or null for native metadata. Needs the GIL. */
template <typename Info>
py::object transferredObject(Info* info) {
	if (!module::transferredIn(info))
		return py::object();
	// The disowned instance stays registered, so this is the same Python object.
	return py::cast(info, py::return_value_policy::reference);
}

}  // namespace bindings
}  // namespace rackpy
