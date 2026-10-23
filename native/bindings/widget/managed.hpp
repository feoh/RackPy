// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <rack.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/trampoline_self_life_support.h>
#include <memory>
#include <functional>
#include <optional>
#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/python.hpp"

// Pybind's default RTTI downcast returns the complete alias address. Our
// aliases put the SDK base last to order native teardown before peer release;
// pointer round trips must instead keep the registered SDK-base address.
namespace pybind11 {
#define RACKPY_WIDGET_ADDRESS(T) \
 template<> struct polymorphic_type_hook<rack::widget::T> { \
  static const void* get(const rack::widget::T* ptr, const std::type_info*& type) { type = &typeid(rack::widget::T); return ptr; } \
 };
RACKPY_WIDGET_ADDRESS(Widget)
RACKPY_WIDGET_ADDRESS(TransparentWidget)
RACKPY_WIDGET_ADDRESS(OpaqueWidget)
RACKPY_WIDGET_ADDRESS(ZoomWidget)
RACKPY_WIDGET_ADDRESS(TransformWidget)
RACKPY_WIDGET_ADDRESS(SvgWidget)
RACKPY_WIDGET_ADDRESS(FramebufferWidget)
RACKPY_WIDGET_ADDRESS(OpenGlWidget)
#undef RACKPY_WIDGET_ADDRESS
}

namespace rackpy { namespace widgets {
namespace py = pybind11;
using W = rack::widget::Widget;

struct State {
	W* native = nullptr;
	const void* peerPointer = nullptr;
	const std::type_info* peerType = nullptr;
	std::shared_ptr<runtime::ObjectToken> token;
	runtime::PyRef weak;
	bool transferred = false;
	size_t graphicsGeneration = 0;
	int drawing = 0;
	bool clearing = false;
	bool pythonOwned = true;
	std::function<py::object()> reclaim;
	std::function<W*(py::handle)> disown;
};
struct DrawGuard {
	std::shared_ptr<State> state;
	explicit DrawGuard(std::shared_ptr<State> owner);
	~DrawGuard();
};

std::shared_ptr<State> createState(W* native, const void* peer, const std::type_info& type);
std::shared_ptr<State> state(py::handle self);
std::shared_ptr<State> state(W* native);
W& checked(const std::shared_ptr<State>& s);
void destroyed(const std::shared_ptr<State>& s) noexcept;
void retire(const std::shared_ptr<State>& s) noexcept;
py::object wrapper(W* native);
W* argument(py::handle value);
struct Target {
	W* value = nullptr;
	std::function<W*()> resolve;
	W* get() const { return resolve ? resolve() : nullptr; }
};
Target target(W* native);
bool drawingActive();
void checkMutation();
void checkTraversal(const std::shared_ptr<State>& owner);
void checkStep(const std::shared_ptr<State>& owner);
void bindManagedWidgets(py::module_ mod);
/** Hand a managed Python widget to a Rack-owned parent (the scene, a module widget). */
void transferToHost(W& parent, py::object child);

struct UiDelete {
	std::shared_ptr<State> owner;
	void operator()(W*) const noexcept { retire(owner); }
};
using Owned = std::unique_ptr<W, UiDelete>;

// No ownership changes while Rack is iterating children or dispatching a
// borrowed callback. requestDelete remains available for deferred deletion.
struct CallbackGuard {
	const State* previous;
	explicit CallbackGuard(const State* allowedStep = nullptr);
	~CallbackGuard();
};

struct Managed {
	std::shared_ptr<State> lifetime;
	// Engaged by the alias destructor, and destroyed only after the native
	// base and Python self-life-support have finished (including __del__).
	std::optional<CallbackGuard> destructionGuard{};
	virtual ~Managed() = default;
};
// Destruction order matters: native Base must finish before invalidation,
// and invalidation must precede the Python peer's final DECREF/__del__.
struct Invalidate {
	std::shared_ptr<State>* tracked = nullptr;
	~Invalidate() { if (tracked) destroyed(*tracked); }
};


} }  // namespace rackpy::widgets
