// SPDX-License-Identifier: GPL-3.0-or-later
#include "trampoline.hpp"
#include "behavior.hpp"
#include "../../widget/geometry.hpp"
#include <pybind11/stl.h>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "rackpy/runtime/interpreter.hpp"

namespace rackpy { namespace widgets {
namespace {
std::unordered_map<W*, std::shared_ptr<State>> nativeStates;
std::unordered_map<PyObject*, std::shared_ptr<State>> pythonStates;
std::mutex pendingMutex;
std::vector<std::shared_ptr<State>> pending;
int callbackDepth = 0;
int drawingDepth = 0;
const State* allowedStep = nullptr;
bool initialized = false;
bool closing = false;

struct ExternalState {
	rack::WeakPtr<W> weak;
	std::shared_ptr<runtime::ContextToken> context;
};
struct ExternalWidget { std::shared_ptr<ExternalState> owner; };
std::unordered_map<W*, std::shared_ptr<ExternalState>> externalStates;

W& checkedExternal(const ExternalWidget& ref) {
	runtime::requireMainThread("using a borrowed host Widget");
	runtime::checkContext(*ref.owner->context);
	auto* w = ref.owner->weak.get();
	if (!w) throw runtime::StaleHandle("borrowed host Widget has been deleted");
	return *w;
}

void deleteDetached(const std::shared_ptr<State>& s) {
	// EventState can retain a detached widget (for example after raw tree
	// edits). Clear those references before freeing it, not merely its children.
	if (auto* ctx = rack::contextGet(); ctx && ctx->event) {
		CallbackGuard guard;
		ctx->event->finalizeWidget(s->native);
	}
	delete s->native;
}

void drain() {
	runtime::requireMainThread("collecting Widget objects");
	if (callbackDepth || drawingDepth) return;
	auto context = runtime::binding().current();
	if (!context || context->context != rack::contextGet()) return;
	std::vector<std::shared_ptr<State>> local;
	{
		std::lock_guard<std::mutex> lock(pendingMutex);
		local.swap(pending);
	}
	for (const auto& s : local)
		if (s->native && s->token->alive())
			deleteDetached(s);
}

void close(py::object self) {
	auto s = state(self);
	if (!s->token->alive())
		return;
	checkMutation();
	auto& w = checked(s);
	if (w.parent)
		throw runtime::Ownership("remove Widget from its parent before closing it");
	// Extraction activates trampoline self-life-support. Native destruction
	// clears the pybind value pointer before a retained Python wrapper can run.
	if (!s->pythonOwned) { retire(s); return; }
	Owned owned(s->disown(self), UiDelete{s});
	owned.reset();
}

void transferTo(W& parent, const std::shared_ptr<runtime::ContextToken>& context,
    py::object child, int placement, W* sibling = nullptr) {
    checkMutation();
    auto c = state(child);
    auto& item = checked(c);
    if (context != c->token->context)
        throw runtime::WrongContext("widgets belong to different contexts");
    if (item.parent || &item == &parent || parent.isDescendantOf(&item))
        throw runtime::Ownership("child must be detached and cannot be its own ancestor");
    if (placement >= 2 && (!sibling || sibling->parent != &parent))
        throw runtime::Ownership("sibling must be a direct child of this parent");
    Owned owned(c->disown(child), UiDelete{c});
    try {
        CallbackGuard guard;
        if (placement == 1) parent.addChildBottom(owned.get());
        else if (placement == 2) parent.addChildAbove(owned.get(), sibling);
        else if (placement == 3) parent.addChildBelow(owned.get(), sibling);
        else parent.addChild(owned.get());
    }
    catch (...) {
        item.parent = nullptr;
        owned.release();
        c->reclaim();
        throw;
    }
    owned.release();
    if (!c->transferred) {
        c->transferred = true;
        runtime::noteTransferred();
    }
}

void transfer(py::object self, py::object child, int placement, py::object sibling = py::none()) {
    auto p = state(self);
    transferTo(checked(p), p->token->context, child, placement, argument(sibling));
}
void detach(py::object self) {
    checkMutation();
    auto s = state(self);
    auto& item = checked(s);
    if (!item.parent) return;
    {
        CallbackGuard guard;
        item.parent->removeChild(&item);
    }
    s->reclaim();
}

void remove(py::object self, py::object child) {
	checkMutation();
	auto p = state(self), c = state(child);
	auto& parent = checked(p);
	auto& item = checked(c);
	if (item.parent != &parent)
		throw runtime::Ownership("Widget is not a direct child of this parent");
	{
		CallbackGuard guard;
		parent.removeChild(&item);
	}
	// Reclaim an existing disowned alias, preserving the Python subclass and
	// identity rather than manufacturing a second owning wrapper.
	c->reclaim();
}

void initialize() {
	if (initialized) return;
	initialized = true;
	runtime::onUiStep([] { drain(); });
	runtime::onGraphicsContextDestroy([] {
		for (const auto& pair : nativeStates) {
			auto& s = pair.second;
			++s->graphicsGeneration;
			// A Python override may omit super().on_context_destroy(). Owned
			// native FBOs must still die before Rack destroys the GL context.
			if (auto* fb = dynamic_cast<rack::widget::FramebufferWidget*>(pair.first))
				fb->deleteFramebuffer();
		}
	});
	runtime::onGraphicsContextCreate([] {
		for (const auto& pair : nativeStates) {
			++pair.second->graphicsGeneration;
			if (auto* fb = dynamic_cast<rack::widget::FramebufferWidget*>(pair.first))
				fb->dirty = true;
		}
	});
	runtime::onContextClosing([] {
		closing = true;
		drain();
		runtime::callPython("closing managed Widgets", [] {
			std::vector<W*> roots;
			for (const auto& pair : nativeStates)
				if (!pair.first->parent || !nativeStates.count(pair.first->parent))
					roots.push_back(pair.first);
			for (auto* root : roots) {
				if (!nativeStates.count(root)) continue;
				auto self = wrapper(root);
				if (root->parent) detach(self);
				close(self);
			}
		});
		for (auto& pair : externalStates) pair.second->weak.set(nullptr);
		externalStates.clear();
	});
	runtime::onFinalize([] {
		pythonStates.clear();
		{
			std::lock_guard<std::mutex> lock(pendingMutex);
			pending.clear();
		}
		initialized = false;
	});
}
}

DrawGuard::DrawGuard(std::shared_ptr<State> owner) : state(std::move(owner)) { ++state->drawing; ++drawingDepth; }
DrawGuard::~DrawGuard() { --state->drawing; --drawingDepth; }
bool drawingActive() { return drawingDepth != 0; }
CallbackGuard::CallbackGuard(const State* step) : previous(allowedStep) { ++callbackDepth; allowedStep = step; }
CallbackGuard::~CallbackGuard() { --callbackDepth; allowedStep = previous; }
void checkStep(const std::shared_ptr<State>& owner) {
	if (callbackDepth && allowedStep != owner.get())
		throw runtime::Ownership("reentrant stepping during a callback can invalidate Rack's child iterator; only the current step base call is allowed");
}
void checkTraversal(const std::shared_ptr<State>& owner) {
	if (owner->clearing)
		throw runtime::Ownership("Widget children are being cleared; traversal would observe destroyed native entries");
}
void checkMutation() {
	runtime::requireMainThread("changing Widget ownership");
	if (callbackDepth)
		throw runtime::Ownership("synchronous tree mutation during a Widget callback is unsafe; request deletion instead");
}

std::shared_ptr<State> createState(W* native, const void* peer, const std::type_info& type) {
	runtime::requireMainThread("constructing a Widget");
	if (closing) throw runtime::StaleHandle("Widget construction during context shutdown is not allowed");
	auto* ctx = rack::contextGet();
	if (!ctx || !ctx->event || !ctx->scene)
		throw runtime::WrongContext("Widgets require Rack's live UI context");
	auto context = runtime::bindCurrentContext();
	if (!context) throw runtime::WrongContext("Widget belongs to a foreign context");
	runtime::checkContext(*context);
	initialize();
	auto s = std::make_shared<State>();
	s->native = native;
	s->peerPointer = peer;
	s->peerType = &type;
	s->token = std::make_shared<runtime::ObjectToken>(context);
	nativeStates.emplace(native, s);
	return s;
}

std::shared_ptr<State> state(W* native) {
	auto it = nativeStates.find(native);
	if (it == nativeStates.end())
		throw runtime::Ownership("Widget is not owned by this binding");
	return it->second;
}
std::shared_ptr<State> state(py::handle self) {
	runtime::requireMainThread("using a Widget");
	auto it = pythonStates.find(self.ptr());
	if (it != pythonStates.end()) return it->second;
	auto s = state(self.cast<W*>());
	auto key = self.ptr();
	s->weak = runtime::PyRef(py::weakref(self, py::cpp_function([key](py::handle) { pythonStates.erase(key); })));
	pythonStates.emplace(key, s);
	return s;
}
W& checked(const std::shared_ptr<State>& s) {
	runtime::requireMainThread("using a Widget");
	drain();
	runtime::checkContext(*s->token->context);
	if (!s->native || !s->token->alive())
		throw runtime::StaleHandle("Widget has been deleted by its native owner");
	return *s->native;
}
py::object wrapper(W* native) {
	if (!native) return py::none();
	runtime::requireMainThread("viewing a Widget");
	if (nativeStates.count(native)) {
		auto s = state(native);
		checked(s);
		// Use the registered native-base address, not RTTI's complete alias
		// address. The alias intentionally places Base last so native child
		// teardown finishes before the Python peer is released. This is the
		// same instance-map lookup used by pinned pybind11's get_override().
		auto handle = py::detail::get_object_handle(s->peerPointer, py::detail::get_type_info(*s->peerType));
		if (handle) {
			auto self = py::reinterpret_borrow<py::object>(handle);
			state(self);
			return self;
		}
		// A worker's last Python owner can have gone away before deferred
		// native teardown. Its children can still borrow the live SDK base.
		// Do not manufacture a second owning/subclass wrapper.
	}
	auto& owner = externalStates[native];
	if (!owner || !owner->weak.get()) {
		if (owner) owner->weak.set(nullptr);
		owner = std::make_shared<ExternalState>();
		owner->context = runtime::binding().current();
		if (!owner->context) throw runtime::WrongContext("no live Widget context");
		runtime::checkContext(*owner->context);
		owner->weak.set(native);
	}
	return runtime::identity(owner.get(), owner->context->generation, [&] {
		return py::cast(ExternalWidget{owner});
	});
}
W* argument(py::handle value) {
	if (value.is_none()) return nullptr;
	if (py::isinstance<ExternalWidget>(value))
		return &checkedExternal(value.cast<const ExternalWidget&>());
	return &checked(state(value));
}
Target target(W* native) {
	if (!native) return {};
	auto it = nativeStates.find(native);
	if (it != nativeStates.end()) {
		auto s = it->second;
		return Target{native, [s] { return &checked(s); }};
	}
	// Register a weak host identity while the source event guarantees liveness.
	wrapper(native);
	auto owner = externalStates.at(native);
	return Target{native, [owner] { return &checkedExternal(ExternalWidget{owner}); }};
}
void destroyed(const std::shared_ptr<State>& s) noexcept {
	if (!s) return;
	s->token->invalidate();
	nativeStates.erase(s->native);
	s->native = nullptr;
	if (s->transferred) runtime::noteTransferredDeleted();
}
void retire(const std::shared_ptr<State>& s) noexcept {
	if (!s || !s->native || !s->token->alive()) return;
	if (runtime::isMainThread() && !callbackDepth && !drawingDepth && rack::contextGet() == s->token->context->context) {
		try { deleteDetached(s); }
		catch (...) { runtime::detail::noteRaised("Widget deletion", "native EventState finalization failed"); }
		return;
	}
	try {
		std::lock_guard<std::mutex> lock(pendingMutex);
		pending.push_back(s);
	}
	catch (...) {
		// Do not delete GUI objects on a Python worker if allocation failed.
		// The context-closing root sweep remains the owner and reclaims it.
	}
}

bool managedWidget(W* w) { return nativeStates.count(w) != 0; }

void transferToHost(W& parent, py::object child) {
	auto s = state(child);
	checked(s);
	transferTo(parent, s->token->context, child, 0);
}

W& geometrySetter(py::handle self) {
	auto s = state(self);
	checkTraversal(s);
	return checked(s);
}

/** Rack's Widget::children as a checked, live, read-only sequence (owner
decision 2026-10-24). Every access resolves the parent again, so the view sees
later additions and removals and raises once the parent is gone; it does not
keep the native parent alive. Changes go through add_child and its siblings,
which keep each child's single owner. */
struct ChildrenView {
	std::shared_ptr<State> owner;
	W& parent() const {
		W& w = checked(owner);  // a deleted parent is stale, even if it was deleted while clearing
		checkTraversal(owner);
		return w;
	}
	py::tuple snapshot() const {
		auto& w = parent();
		py::tuple result(w.children.size());
		size_t i = 0;
		for (auto* child : w.children) result[i++] = wrapper(child);
		return result;
	}
};

void bindChildrenView(py::module_ mod) {
	auto cls = py::class_<ChildrenView>(mod, "WidgetChildren", py::is_final(),
		"Rack's Widget::children: a live, read-only, checked sequence. Iteration walks the children present when it "
		"starts; change the list with add_child, remove_child and the other ownership methods.")
		.def("__len__", [](const ChildrenView& v) { return v.parent().children.size(); })
		.def("__bool__", [](const ChildrenView& v) { return !v.parent().children.empty(); })
		.def("__getitem__", [](const ChildrenView& v, py::ssize_t index) {
			auto& children = v.parent().children;
			auto size = (py::ssize_t) children.size();
			if (index < 0) index += size;
			if (index < 0 || index >= size) throw py::index_error("Widget child index out of range");
			return wrapper(*std::next(children.begin(), index));
		}, py::arg("index"))
		.def("__getitem__", [](const ChildrenView& v, py::slice slice) { return py::object(v.snapshot()[slice]); }, py::arg("index"))
		.def("__iter__", [](const ChildrenView& v) { return py::iter(v.snapshot()); })
		.def("__reversed__", [](const ChildrenView& v) {
			return py::module_::import("builtins").attr("reversed")(v.snapshot());
		})
		.def("__contains__", [](const ChildrenView& v, py::handle value) {
			for (auto* child : v.parent().children)
				if (wrapper(child).is(value)) return true;
			return false;
		}, py::arg("value"))
		.def("index", [](const ChildrenView& v, py::object value) { return v.snapshot().attr("index")(value); },
			py::arg("value"))
		.def("count", [](const ChildrenView& v, py::object value) { return v.snapshot().attr("count")(value); },
			py::arg("value"))
		.def("__eq__", [](const ChildrenView& v, py::object other) -> py::object {
			if (py::isinstance<ChildrenView>(other))
				return py::bool_(v.snapshot().equal(other.cast<const ChildrenView&>().snapshot()));
			if (py::isinstance<py::tuple>(other))
				return py::bool_(v.snapshot().equal(other));
			return py::reinterpret_borrow<py::object>(Py_NotImplemented);
		}, py::is_operator())
		.def("copy", [](const ChildrenView& v) { return v.snapshot(); }, "The children now, as a tuple.")
		.def("__copy__", [](const ChildrenView& v) { return v.snapshot(); })
		.def("__repr__", [](const ChildrenView& v) {
			return "WidgetChildren(" + py::repr(v.snapshot()).cast<std::string>() + ")";
		});
	cls.attr("__hash__") = py::none();
	py::module_::import("collections.abc").attr("Sequence").attr("register")(cls);
}

void bindManagedWidgets(py::module_ mod) {
	bindChildrenView(mod);
	py::class_<ExternalWidget>(mod, "HostWidget", py::is_final(),
		"Nonowning, checked host Widget identity; cannot transfer or destroy Rack-owned nodes.")
		.def_property_readonly("alive", [](const ExternalWidget& ref) {
			runtime::requireMainThread("checking a host Widget");
			return ref.owner->context->alive() && ref.owner->weak.get() != nullptr;
		})
		.def("get_box", [](const ExternalWidget& ref) { return checkedExternal(ref).getBox(); })
		.def_property_readonly("parent", [](const ExternalWidget& ref) { return wrapper(checkedExternal(ref).parent); });
	auto cls = py::classh<W, Widget<W>>(mod, "Widget", py::dynamic_attr(), "Subclassable, context-bound Rack Widget.")
		.def(py::init([] {
			auto* w = new Widget<W>();
			return Owned(w, UiDelete{w->lifetime});
		}))
		.def_property_readonly("alive", [](py::object self) {
			auto s = state(self);
			drain();
			return s->token->alive();
		})
		.def("close", &close)
		.def("add_child", [](py::object self, py::object child) { transfer(self, child, 0); })
		.def("add_child_bottom", [](py::object self, py::object child) { transfer(self, child, 1); })
        .def("add_child_above", [](py::object self, py::object child, py::object sibling) { transfer(self, child, 2, sibling); },
            py::arg("child"), py::arg("sibling"))
        .def("add_child_below", [](py::object self, py::object child, py::object sibling) { transfer(self, child, 3, sibling); },
            py::arg("child"), py::arg("sibling"))
        .def("remove_from_parent", &detach)
        .def("attach_to_scene", [](py::object self) {
            auto s = state(self);
            checked(s);
            transferToHost(*s->token->context->context->scene, self);
        })
		.def("remove_child", &remove)
		.def_property_readonly("children", [](py::object self) {
			ChildrenView view{state(self)};
			view.parent();
			return view;
		}, "Rack's children: a live, read-only WidgetChildren view; copy() takes a tuple snapshot.")
		.def_property_readonly("parent", [](py::object self) { return wrapper(checked(state(self)).parent); })
		.def("clear_children", [](py::object self) {
			checkMutation();
			auto s = state(self);
			checkTraversal(s);
			auto& w = checked(s);
			CallbackGuard guard;
			s->clearing = true;
			try { w.clearChildren(); }
			catch (...) { s->clearing = false; throw; }
			s->clearing = false;
		})

		.def("request_delete", [](py::object self) { checked(state(self)).requestDelete(); })
		.def("get_box", [](py::object self) { return checked(state(self)).getBox(); })
        .def("set_box", [](py::object self, rack::math::Rect box) { geometrySetter(self).setBox(box); }, py::arg("box"))
        .def_property("box", [](py::object self) {
            auto s = state(self);
            checked(s);
            return widget::BoxView{widget::GeometryOwner(std::function<W&()>([s]() -> W& { return checked(s); }))};
        }, [](py::object self, rack::math::Rect box) { checked(state(self)).box = box; })
        .def_property("visible", [](py::object self) { return checked(state(self)).visible; },
            [](py::object self, bool value) { checked(state(self)).visible = value; })
        .def_property("requested_delete", [](py::object self) { return checked(state(self)).requestedDelete; },
            [](py::object self, bool value) { checked(state(self)).requestedDelete = value; })
        .def("get_parent", [](py::object self) { return wrapper(checked(state(self)).getParent()); })
        .def("get_position", [](py::object self) { return checked(state(self)).getPosition(); })
        .def("set_position", [](py::object self, rack::math::Vec value) { geometrySetter(self).setPosition(value); }, py::arg("position"))
        .def("get_size", [](py::object self) { return checked(state(self)).getSize(); })
        .def("set_size", [](py::object self, rack::math::Vec value) { geometrySetter(self).setSize(value); }, py::arg("size"))
        .def("is_visible", [](py::object self) { return checked(state(self)).isVisible(); })
        .def("set_visible", [](py::object self, bool value) { geometrySetter(self).setVisible(value); }, py::arg("visible"))
        .def("show", [](py::object self) { geometrySetter(self).show(); })
        .def("hide", [](py::object self) { geometrySetter(self).hide(); })
        .def("has_child", [](py::object self, py::object child) { return checked(state(self)).hasChild(argument(child)); }, py::arg("child"))
        .def("is_descendant_of", [](py::object self, py::object ancestor) { return checked(state(self)).isDescendantOf(argument(ancestor)); }, py::arg("ancestor"))
        .def("get_absolute_offset", [](py::object self, rack::math::Vec value) { return checked(state(self)).getAbsoluteOffset(value); }, py::arg("position"))
        .def("get_absolute_zoom", [](py::object self) { return checked(state(self)).getAbsoluteZoom(); })
        .def("draw_child", [](py::object self, py::object child, py::object args, int layer) {
            auto s = state(self);
            auto& w = checked(s);
            auto* c = argument(child);
            if (!c) throw py::type_error("draw_child requires a non-null Widget");
            auto heldArgs = drawArgs(args);
            CallbackGuard guard;
            DrawGuard drawing(s);
            w.drawChild(c, *static_cast<const W::DrawArgs*>(heldArgs->ptr), layer);
        }, py::arg("child"), py::arg("args"), py::arg("layer") = 0);
	bindBehavior(cls);
}
} }  // namespace rackpy::widgets
