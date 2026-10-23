// SPDX-License-Identifier: GPL-3.0-or-later
// Engine components (Port.hpp, Param.hpp, Light.hpp, PortInfo.hpp,
// LightInfo.hpp) -> rackpy.raw.engine, RP-13.
//
// A component in Python is a checked reference: a pointer plus whatever keeps
// the memory valid. Created in Python, it owns its storage. Borrowed from a
// Rack module (RP-14 hands these out), it shares the module's ObjectToken and
// raises StaleHandleError once the module is deleted, never touching freed
// memory. Repeated views of one component are the same Python object. A
// Rack-owned component is used on the main thread only (see get()).
#pragma once
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include <memory>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;

template <typename T>
struct ComponentRef {
	T* ptr = nullptr;
	std::shared_ptr<void> storage;  // set when Python owns the component
	std::shared_ptr<runtime::ObjectToken> owner;  // set when a Rack object owns it
	const char* type = "component";

	/** The component, or StaleHandle if its owner is gone.

	A Rack-owned component is usable on the main thread only. Rack deletes modules
	on the main thread, so there the liveness check and the access cannot race a
	deletion; on another thread they could, so WrongThread is raised instead. */
	T& get() const {
		if (owner) {
			runtime::requireMainThread("using a Rack-owned component");
			if (!owner->alive())
				throw runtime::StaleHandle(std::string("this ") + type + " belonged to a module that Rack has deleted");
		}
		return *ptr;
	}
	bool owned() const { return storage != nullptr; }
	bool alive() const { return !owner || owner->alive(); }

	/** A reference to a member of this component (a plug light, the voltage array), with the same lifetime. */
	template <typename U>
	ComponentRef<U> member(U* p, const char* memberType) const {
		return ComponentRef<U>{p, storage, owner, memberType};
	}
};

using PortRef = ComponentRef<rack::engine::Port>;
using ParamRef = ComponentRef<rack::engine::Param>;
using LightRef = ComponentRef<rack::engine::Light>;

/** Python views of Rack-owned components, one Python object per (component, owner). Needs the GIL. */
py::object borrowInput(rack::engine::Input* port, std::shared_ptr<runtime::ObjectToken> owner);
py::object borrowOutput(rack::engine::Output* port, std::shared_ptr<runtime::ObjectToken> owner);
py::object borrowParam(rack::engine::Param* param, std::shared_ptr<runtime::ObjectToken> owner);
py::object borrowLight(rack::engine::Light* light, std::shared_ptr<runtime::ObjectToken> owner);

/** A Python component's value, for a raw module vector element (C++ copy
assignment; RP-14B2b7). TypeError for another component class, StaleHandle
for a view of a deleted component. Needs the GIL. */
rack::engine::Param paramValue(py::handle value);
rack::engine::Input inputValue(py::handle value);
rack::engine::Output outputValue(py::handle value);
rack::engine::Light lightValue(py::handle value);

void bindComponents(py::module_ engine);

}  // namespace bindings
}  // namespace rackpy
