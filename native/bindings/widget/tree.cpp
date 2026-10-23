// SPDX-License-Identifier: GPL-3.0-or-later
#include "tree.hpp"
#include "../../widget/tree.hpp"

#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {
namespace py = pybind11;
namespace {

using Handle = widget::Handle;
std::weak_ptr<widget::Manager> active;

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
	py::class_<Handle, std::shared_ptr<Handle>>(mod, "WidgetNode", py::is_final(),
		"A detached native base Widget with checked tree ownership. No Python subclass callbacks or scene insertion.")
		.def_static("create", [] { return object(manager()->create()); })
		.def_property_readonly("alive", [](const Handle& h) {
			runtime::requireMainThread("checking widget lifetime");
			if (!h.token->alive())
				return false;
			h.manager->drain();
			return h.token->alive();
		})
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
		.def("remove_child", [](const Handle& h, const Handle& c) { h.manager->remove(h, c); }, py::arg("child"))
		.def("clear_children", [](const Handle& h) { h.get().clearChildren(); })
		.def("has_child", [](const Handle& h, const Handle& c) { return h.get().hasChild(&c.get()); }, py::arg("child"))
		.def("get_box", [](const Handle& h) { return h.get().getBox(); })
		.def("set_box", [](const Handle& h, rack::math::Rect r) { h.get().setBox(r); }, py::arg("box"))
		.def("is_visible", [](const Handle& h) { return h.get().isVisible(); })
		.def("set_visible", [](const Handle& h, bool visible) { h.get().setVisible(visible); }, py::arg("visible"))
		.def("get_children_bounding_box", [](const Handle& h) { return h.get().getChildrenBoundingBox(); })
		.def("get_visible_children_bounding_box", [](const Handle& h) { return h.get().getVisibleChildrenBoundingBox(); });
}

}  // namespace bindings
}  // namespace rackpy
