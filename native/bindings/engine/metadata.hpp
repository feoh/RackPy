// SPDX-License-Identifier: GPL-3.0-or-later
// Python-created PortInfo and LightInfo: standalone objects with weak module
// links (RP-14B2b2), and objects a module slot owns after a class factory
// transferred them (RP-14B2b5).
#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/trampoline_self_life_support.h>

#include <functional>
#include <memory>
#include <string>

#include "module.hpp"
#include "../support/overrides.hpp"
#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;

/** Python-created metadata or quantities that a class factory moved into a
module slot (RP-14B2b5/b6). `Base` is the bound SDK type, `Alias` the
trampoline deriving from both. Transferred, the slot owns the native object
and keeps its Python half alive; Rack-owned objects are used on the main
thread only, like a module's component views. Retired, the slot has handed
it back: the Python object survives while referenced, but every bound access
raises StaleHandle. */
template <typename Base, typename Alias>
struct SlotTransfer : module::TransferredMetadata {
	// What the `module` field points at (with a link when Python set it), and
	// separately the module whose slot owns the object.
	std::weak_ptr<module::Construction> moduleState;
	std::shared_ptr<module::ModuleBackreference> moduleLink;
	std::weak_ptr<module::Construction> slotOwner;
	bool inSlot = false;
	bool retired = false;

	~SlotTransfer() override {
		if (moduleLink)
			moduleLink->detach();
	}
	bool transferred() const override { return inSlot; }
	/** Rack calls a slot's virtuals without the GIL, possibly off the main
	thread: those calls get native semantics and must never throw. */
	bool rackCaller() const { return inSlot && !PyGILState_Check(); }
	void check(const char* what) const {
		if (!inSlot && !retired)
			return;
		runtime::requireMainThread(what);
		if (retired)
			throw runtime::StaleHandle("this object was replaced or its module was deleted");
	}
	void detachLink() {
		if (moduleLink)
			moduleLink->detach();
		moduleLink.reset();
	}
	/** Committed transfer into `state`'s slot: nothing here throws. A class
	factory then points `module` at the owner, so a link to another module
	must go; a raw vector assignment leaves the field exactly as it was. */
	void enterSlot(const std::shared_ptr<module::Construction>& state, bool factory) {
		if (factory) {
			detachLink();
			moduleState = state;
		}
		slotOwner = state;
		inSlot = true;
		runtime::noteTransferred();
	}
	/** Identity fields (module, IDs, port type) of a slot's object are frozen
	once its module is sealed: Rack's engine and UI index with them unchecked. */
	void requireIdentityEdit() const {
		if (!inSlot)
			return;
		auto owner = slotOwner.lock();
		if (owner && !owner->constructing)
			throw runtime::Ownership("a module's metadata identity (module, IDs, port type) can change only during construction");
	}
	/** Raw vector removal: Python owns the object again and it stays usable.
	Its module pointer is kept, as in C++, but tracked, so deleting the module
	later clears it instead of leaving it dangling. */
	void release() noexcept override {
		if (!runtime::interpreterAlive()) {
			runtime::diagnose(runtime::DiagnosticCode::ShutdownLeak, "transferred metadata outlived Python");
			return;
		}
		PyGILState_STATE gil = PyGILState_Ensure();
		Base* self = static_cast<Alias*>(this);
		inSlot = false;
		slotOwner.reset();
		if (self->module && !moduleLink) {
			try {
				auto state = moduleState.lock();
				if (state && state->object && state->object.get() == self->module)
					moduleLink = state->object->trackBackreference(&self->module);
				else
					self->module = nullptr;
			}
			catch (...) {
				self->module = nullptr;  // cannot track it: never leave it dangling
			}
		}
		runtime::noteTransferredDeleted();
		reclaim(self);
		PyGILState_Release(gil);
	}
	static void reclaim(Base* self) noexcept {
		try {
			// The slot's ownership returns to the existing Python object. Its
			// last reference may go here and run Python finalizers: the slot is
			// already empty and the object's flags already say so.
			py::object owner = py::cast(std::unique_ptr<Base>(self));
		}
		catch (...) {
			// A failed reclaim leaves the unique_ptr owning, which deleted the
			// object; pybind11's self-life support released the Python half.
			runtime::diagnose(runtime::DiagnosticCode::CallbackRaised, "reclaiming transferred metadata failed");
		}
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
		Base* self = static_cast<Alias*>(this);
		inSlot = false;
		retired = true;
		slotOwner.reset();
		self->module = nullptr;
		moduleState.reset();
		detachLink();
		runtime::noteTransferredDeleted();
		reclaim(self);
		PyGILState_Release(gil);
	}
};

/** Python overrides of the info classes' virtual names (callback strategy "ui").
Standalone, Python owns the object and only `module` is main-thread-only. */
template <typename Base>
struct InfoTrampoline : Base, SlotTransfer<Base, InfoTrampoline<Base>>, py::trampoline_self_life_support {
	using Base::Base;
	void check() const { SlotTransfer<Base, InfoTrampoline<Base>>::check("using metadata that a module owns"); }
	std::string getName() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_name", [this] { return Base::getName(); });
	}
	std::string getDescription() override {
		return overrideOr<std::string>(static_cast<const Base*>(this), "get_description", [this] { return Base::getDescription(); });
	}
};

/** Construct `type()` for a slot, as Rack's `new T` does. `argument` names the
Python parameter in errors. It must be `base` or a Python subclass, and return
a new instance of exactly that class. The
constructor may reenter the module, so `recheck` runs before anything else. */
template <typename Bound>
py::object constructForSlot(py::handle type, const char* argument, const char* base, const std::function<void()>& recheck) {
	py::object cls = py::type::of<Bound>();
	int subclass = PyType_Check(type.ptr()) ? PyObject_IsSubclass(type.ptr(), cls.ptr()) : 0;
	if (subclass < 0)
		throw py::error_already_set();
	if (!subclass)
		throw py::type_error(std::string(argument) + " must be " + base + " or a subclass of it");
	py::object instance = type();
	recheck();
	if (reinterpret_cast<PyObject*>(Py_TYPE(instance.ptr())) != type.ptr())
		throw py::type_error(std::string(argument) + "() must return a new instance of exactly " + argument);
	return instance;
}

/** Disown a Python-created alias for a slot. The flags are checked first.
`factory` is false for a raw vector assignment, which sets no fields. */
template <typename Base, typename Alias>
std::unique_ptr<Base> disownForSlot(py::object instance, Alias& alias, const std::shared_ptr<module::Construction>& state,
	bool factory = true) {
	if (alias.retired)
		throw runtime::StaleHandle("the class returned an object that a module slot already released");
	if (alias.inSlot)
		throw runtime::Ownership("the class returned an object that a module slot already owns");
	std::unique_ptr<Base> owned;
	try {
		owned = instance.cast<std::unique_ptr<Base>>();
	}
	catch (const std::exception& e) {
		throw runtime::Ownership(std::string("this object cannot be transferred to a module (") + e.what() + ")");
	}
	alias.enterSlot(state, factory);
	return owned;
}

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
