// SPDX-License-Identifier: GPL-3.0-or-later
#include "tree.hpp"
#include "../../widget/geometry.hpp"
#include "managed.hpp"
#include "events.hpp"
#include "variants.hpp"

#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {
namespace py = pybind11;
namespace {

using Handle = widget::Handle;
using BoxView = widget::BoxView;
using VecView = widget::VecView;
std::weak_ptr<widget::Manager> active;

rack::math::Rect rectValue(py::handle value) {
	if (py::isinstance<BoxView>(value))
		return value.cast<const BoxView&>().get();
	if (!py::isinstance<rack::math::Rect>(value))
		throw py::type_error("box must be a Rect or WidgetBoxView");
	return value.cast<rack::math::Rect>();
}

rack::math::Vec vecValue(py::handle value) {
	if (py::isinstance<VecView>(value))
		return value.cast<const VecView&>().get();
	if (!py::isinstance<rack::math::Vec>(value))
		throw py::type_error("vector must be a Vec or WidgetVecView");
	return value.cast<rack::math::Vec>();
}

void bindGeometryViews(py::module_ mod, py::module_ math) {
	py::class_<VecView>(mod, "WidgetVecView", py::is_final(),
		"Checked write-through Widget box vector; copy() returns an independent Vec.")
		.def_property("x", [](const VecView& v) { return v.get().x; },
			[](const VecView& v, float x) { v.get().x = x; })
		.def_property("y", [](const VecView& v) { return v.get().y; },
			[](const VecView& v, float y) { v.get().y = y; })
		.def("__getitem__", [](const VecView& v, int i) {
			auto& vec = v.get();
			if (i != 0 && i != 1)
				throw py::index_error("Vec index must be 0 (x) or 1 (y)");
			return vec[i];
		})
		.def("__setitem__", [](const VecView& v, int i, float value) {
			auto& vec = v.get();
			if (i != 0 && i != 1)
				throw py::index_error("Vec index must be 0 (x) or 1 (y)");
			vec[i] = value;
		})
		.def("__add__", [](const VecView& v, py::object other) {
			auto value = vecValue(other);
			return v.get() + value;
		})
		.def("__sub__", [](const VecView& v, py::object other) {
			auto value = vecValue(other);
			return v.get() - value;
		})
		.def("__iadd__", [](py::object self, py::object other) {
			auto value = vecValue(other);
			self.cast<const VecView&>().get() += value;
			return self;
		})
		.def("__isub__", [](py::object self, py::object other) {
			auto value = vecValue(other);
			self.cast<const VecView&>().get() -= value;
			return self;
		})
		.def("copy", [](const VecView& v) { return rack::math::Vec(v.get()); })
		.def("__copy__", [](const VecView& v) { return rack::math::Vec(v.get()); })
		.def("__deepcopy__", [](const VecView& v, py::dict) { return rack::math::Vec(v.get()); }, py::arg("memo"));
	py::class_<BoxView>(mod, "WidgetBoxView", py::is_final(),
		"Checked write-through Widget.box; direct writes do not trigger layout events.")
		.def_property("pos", [](const BoxView& v) { v.get(); return VecView{v.owner, false}; },
			[](const BoxView& v, py::object value) { auto copy = vecValue(value); v.get().pos = copy; })
		.def_property("size", [](const BoxView& v) { v.get(); return VecView{v.owner, true}; },
			[](const BoxView& v, py::object value) { auto copy = vecValue(value); v.get().size = copy; })
		.def("copy", [](const BoxView& v) { return rack::math::Rect(v.get()); })
		.def("__copy__", [](const BoxView& v) { return rack::math::Rect(v.get()); })
		.def("__deepcopy__", [](const BoxView& v, py::dict) { return rack::math::Rect(v.get()); }, py::arg("memo"));

	// Value-taking SDK APIs get checked snapshots, never references to borrowed
	// native fields. Numeric operations preserve Rack's value behavior while
	// in-place vector operators remain write-through.
	auto vec = py::reinterpret_borrow<py::class_<VecView>>(mod.attr("WidgetVecView"));
	auto box = py::reinterpret_borrow<py::class_<BoxView>>(mod.attr("WidgetBoxView"));
	for (const char* name : {"neg", "plus", "minus", "mult", "div", "dot", "arg", "norm", "normalize", "square", "area", "rotate",
		"flip", "min", "max", "abs", "round", "floor", "ceil", "equals", "is_zero", "is_finite", "clamp", "clamp_safe", "crossfade", "is_equal",
		"__pos__", "__neg__", "__mul__", "__rmul__", "__truediv__", "__eq__", "__ne__", "__repr__", "__iter__", "__len__"}) {
		vec.def(name, [name](const VecView& view, py::args args, py::kwargs kwargs) {
			return py::cast(rack::math::Vec(view.get())).attr(name)(*args, **kwargs);
		});
	}
	for (const char* name : {"__imul__", "__itruediv__"}) {
		vec.def(name, [name](py::object self, py::object other) {
			auto value = rack::math::Vec(self.cast<const VecView&>().get());
			auto result = py::cast(value).attr(name)(other);
			if (result.is(py::reinterpret_borrow<py::object>(Py_NotImplemented))) return result;
			self.cast<const VecView&>().get() = result.cast<rack::math::Vec>();
			return self;
		});
	}
	for (const char* name : {"contains", "intersects", "equals", "get_left", "get_right", "get_top", "get_bottom", "get_width", "get_height",
		"get_center", "get_top_left", "get_top_right", "get_bottom_left", "get_bottom_right", "clamp", "nudge", "expand", "intersect",
		"zero_pos", "grow", "shrink", "interpolate", "is_containing", "is_intersecting", "is_equal", "__eq__", "__ne__", "__repr__"}) {
		box.def(name, [name](const BoxView& view, py::args args, py::kwargs kwargs) {
			return py::cast(rack::math::Rect(view.get())).attr(name)(*args, **kwargs);
		});
	}
	for (const char* name : {"left", "right", "top", "bottom", "width", "height", "center"})
		box.def_property_readonly(name, [name](const BoxView& view) { return py::cast(rack::math::Rect(view.get())).attr(name); });
	vec.attr("__hash__") = py::none();
	box.attr("__hash__") = py::none();
	auto mathVec = py::reinterpret_borrow<py::class_<rack::math::Vec>>(math.attr("Vec"));
	auto mathRect = py::reinterpret_borrow<py::class_<rack::math::Rect>>(math.attr("Rect"));
	mathVec.def(py::init([](const VecView& view) { return rack::math::Vec(view.get()); }));
	mathRect.def(py::init([](const BoxView& view) { return rack::math::Rect(view.get()); }));
	py::implicitly_convertible<VecView, rack::math::Vec>();
	py::implicitly_convertible<BoxView, rack::math::Rect>();
}

std::shared_ptr<widget::Manager> manager() {
	runtime::requireMainThread("creating a detached widget");
	auto* ctx = rack::contextGet();
	if (!ctx || !ctx->event || !ctx->scene)
		throw runtime::WrongContext("widget trees require Rack's live UI context, not plugin startup");
	auto context = runtime::bindCurrentContext();
	if (!context)
		throw runtime::WrongContext("widget belongs to a foreign Rack context");
	runtime::checkContext(*context);
	if (auto held = active.lock())
		return held;
	auto held = std::make_shared<widget::Manager>(context);
	runtime::onContextClosing([held] { held->shutdown(); });
	runtime::onUiStep([held] { held->drain(); });
	active = held;
	return held;
}

py::object object(const std::shared_ptr<Handle>& h) {
	h->get();
	return runtime::identity(h->ptr, h->token->generation, [&] { return py::cast(h); });
}

rack::widget::Widget* relativeTo(const Handle& owner, py::object other) {
	if (other.is_none())
		return nullptr;
	if (!py::isinstance<Handle>(other))
		throw py::type_error("ancestor must be a WidgetNode or None");
	auto& ancestor = other.cast<const Handle&>();
	auto& node = ancestor.get();
	if (owner.manager.get() != ancestor.manager.get())
		throw runtime::WrongContext("widgets belong to different contexts");
	return &node;
}

}  // namespace

void bindWidgetTrees(py::module_ raw) {
	auto mod = raw.def_submodule("widget", "Partial detached widget adapters, not full widget lifecycle bindings.");
#ifdef RACKPY_TEST_HOOKS
	mod.def("_native_root_count", [] {
		runtime::requireMainThread("inspecting native widget roots");
		auto held = active.lock();
		return held ? held->roots.size() : size_t(0);
	});
#endif
	widgets::bindWidgetEvents(mod);
	bindGeometryViews(mod, raw.attr("math").cast<py::module_>());
	widgets::bindManagedWidgets(mod);
	widgets::bindVariants(raw, mod);
	py::class_<Handle, std::shared_ptr<Handle>>(mod, "WidgetNode", py::is_final(),
		"A detached native base Widget with checked tree ownership and geometry. No Python subclass callbacks or scene insertion.")
		.def_static("create", [] { return object(manager()->create()); })
		.def_property_readonly("alive", [](const Handle& h) {
			runtime::requireMainThread("checking widget lifetime");
			if (!h.token->alive())
				return false;
			h.manager->drain();
			return h.token->alive();
		})
		.def_property("visible", [](const Handle& h) { return h.get().visible; },
			[](const Handle& h, bool visible) { h.get().visible = visible; })
		.def("close", [](const Handle& h) { h.manager->close(h); })
		.def_property_readonly("parent", [](const Handle& h) -> py::object {
			auto* p = h.get().parent;
			return p ? object(h.manager->view(static_cast<widget::Node&>(*p))) : py::none();
		})
		.def_property_readonly("children", [](const Handle& h) {
			auto& n = h.get();
			py::tuple result(n.children.size());
			size_t i = 0;
			for (auto* child : n.children)
				result[i++] = object(h.manager->view(static_cast<widget::Node&>(*child)));
			return result;
		}, "Snapshot of checked handles; does not keep their parent or native storage alive.")
		.def("add_child", [](const Handle& h, const Handle& c) { h.manager->add(h, c); }, py::arg("child"))
		.def("add_child_bottom", [](const Handle& h, const Handle& c) { h.manager->add(h, c, true); }, py::arg("child"))
		.def("add_child_above", [](const Handle& h, const Handle& c, const Handle& s) {
			h.manager->addRelative(h, c, s, true);
		}, py::arg("child"), py::arg("sibling"))
		.def("add_child_below", [](const Handle& h, const Handle& c, const Handle& s) {
			h.manager->addRelative(h, c, s, false);
		}, py::arg("child"), py::arg("sibling"))
		.def("remove_child", [](const Handle& h, const Handle& c) { h.manager->remove(h, c); }, py::arg("child"))
		.def("clear_children", [](const Handle& h) { h.get().clearChildren(); })
		.def("request_delete", [](const Handle& h) { h.get().requestDelete(); })
		.def_property("requested_delete", [](const Handle& h) { return h.get().requestedDelete; },
			[](const Handle& h, bool requested) { h.get().requestedDelete = requested; })
		.def("step", [](const Handle& h) { h.get().step(); },
			"Run the native base Widget step on this detached tree; no Python overrides.")
		.def("has_child", [](const Handle& h, const Handle& c) { return h.get().hasChild(&c.get()); }, py::arg("child"))
		.def_property("box", [](const Handle& h) { h.get(); return BoxView{h}; },
			[](const Handle& h, py::object value) { auto copy = rectValue(value); h.get().box = copy; })
		.def("get_box", [](const Handle& h) { return h.get().getBox(); })
		.def("set_box", [](const Handle& h, rack::math::Rect r) { h.get().setBox(r); }, py::arg("box"))
		.def("is_visible", [](const Handle& h) { return h.get().isVisible(); })
		.def("set_visible", [](const Handle& h, bool visible) { h.get().setVisible(visible); }, py::arg("visible"))
		.def("get_children_bounding_box", [](const Handle& h) { return h.get().getChildrenBoundingBox(); })
		.def("get_visible_children_bounding_box", [](const Handle& h) { return h.get().getVisibleChildrenBoundingBox(); })
		.def("get_position", [](const Handle& h) { return h.get().getPosition(); })
		.def("set_position", [](const Handle& h, rack::math::Vec pos) { h.get().setPosition(pos); }, py::arg("position"))
		.def("get_size", [](const Handle& h) { return h.get().getSize(); })
		.def("set_size", [](const Handle& h, rack::math::Vec size) { h.get().setSize(size); }, py::arg("size"))
		.def("hide", [](const Handle& h) { h.get().hide(); })
		.def("show", [](const Handle& h) { h.get().show(); })
		.def("is_descendant_of", [](const Handle& h, py::object ancestor) {
			return h.get().isDescendantOf(relativeTo(h, std::move(ancestor)));
		}, py::arg("ancestor"))
		.def("get_absolute_offset", [](const Handle& h, rack::math::Vec v) { return h.get().getAbsoluteOffset(v); }, py::arg("position"))
		.def("get_relative_offset", [](const Handle& h, rack::math::Vec v, py::object ancestor) {
			return h.get().getRelativeOffset(v, relativeTo(h, std::move(ancestor)));
		}, py::arg("position"), py::arg("ancestor"))
		.def("get_absolute_zoom", [](const Handle& h) { return h.get().getAbsoluteZoom(); })
		.def("get_relative_zoom", [](const Handle& h, py::object ancestor) {
			return h.get().getRelativeZoom(relativeTo(h, std::move(ancestor)));
		}, py::arg("ancestor"))
		.def("get_viewport", [](const Handle& h, rack::math::Rect rect) { return h.get().getViewport(rect); },
			py::arg("rect") = rack::math::Rect::inf());
}

}  // namespace bindings
}  // namespace rackpy
