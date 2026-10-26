// SPDX-License-Identifier: GPL-3.0-or-later
// rack::WeakPtr, rack::WeakBase and rack::WeakHandle -> rackpy.raw (RP-30C).
// In the rack.hpp closure, Rack's WeakBase is a Widget's base: a WeakPtr
// points at a widget and reads None once Rack deletes it. Main thread (Rack's
// weak pointers are not thread-safe).
#include <pybind11/pybind11.h>

#include <rack.hpp>

#include "managed.hpp"

namespace rackpy {
namespace widgets {

namespace {

using WidgetPtr = rack::WeakPtr<rack::widget::Widget>;

struct WeakPtrRef {
	WidgetPtr ptr;
};

/** The WeakHandle Rack keeps for a widget, through the widget: Rack deletes it with the last WeakPtr. */
struct WeakHandleView {
	WidgetPtr keep;  // holds the handle alive while the view lives
	rack::WeakHandle* get() const {
		runtime::requireMainThread("using a weak handle");
		return keep.weakHandle;
	}
};

rack::widget::Widget* widgetArgument(py::handle value) {
	if (value.is_none())
		return nullptr;
	return argument(value);  // a Python widget or a host view, checked
}

}  // namespace

void bindWeakPointers(py::module_ raw, py::module_ widgetModule) {
	py::class_<WeakHandleView>(raw, "WeakHandle", py::is_final(),
		"Rack's WeakHandle: the object its WeakPtrs share, with their count. Rack makes and deletes them.")
		.def_property_readonly("ptr", [](const WeakHandleView& v) -> py::object {
			auto* h = v.get();
			return h && h->ptr ? wrapper(static_cast<rack::widget::Widget*>(h->ptr)) : py::none();
		}, "Rack's: the widget, or None once Rack deleted it.")
		.def_property_readonly("count", [](const WeakHandleView& v) {
			auto* h = v.get();
			return h ? h->count : 0;
		}, "Rack's: how many WeakPtrs share it (this view holds one).");

	py::class_<WeakPtrRef>(raw, "WeakPtr",
		"Rack's WeakPtr<T>, for widgets: get() returns the widget, or None once Rack deleted it. Main thread.")
		.def(py::init<>())
		.def(py::init([](py::object widget) {
			runtime::requireMainThread("making a weak pointer");
			WeakPtrRef ref;
			ref.ptr.set(widgetArgument(widget));
			return ref;
		}), py::arg("ptr"))
		.def("get", [](const WeakPtrRef& r) {
			runtime::requireMainThread("using a weak pointer");
			return wrapper(r.ptr.get());
		})
		.def("set", [](WeakPtrRef& r, py::object widget) {
			runtime::requireMainThread("using a weak pointer");
			r.ptr.set(widgetArgument(widget));
		}, py::arg("ptr"))
		.def("__bool__", [](const WeakPtrRef& r) {
			runtime::requireMainThread("using a weak pointer");
			return static_cast<bool>(r.ptr);
		}, "Rack's operator bool.")
		.def("__copy__", [](const WeakPtrRef& r) {
			runtime::requireMainThread("copying a weak pointer");
			WeakPtrRef copy;
			copy.ptr = r.ptr;
			return copy;
		}, "Rack's copy: another WeakPtr on the same handle.")
		.def_property_readonly("weak_handle", [](const WeakPtrRef& r) -> py::object {
			runtime::requireMainThread("using a weak pointer");
			if (!r.ptr.weakHandle)
				return py::none();
			WeakHandleView view;
			view.keep = r.ptr;
			return py::cast(view);
		}, "Rack's: the handle it shares, or None.");

	// Rack's WeakBase, as Widget's base: its count and handle.
	for (const char* name : {"Widget", "HostWidget"}) {
		py::object cls = widgetModule.attr(name);
		cls.attr("get_weak_count") = py::cpp_function([](py::object self) {
			runtime::requireMainThread("using a widget");
			return argument(self)->getWeakCount();
		}, py::name("get_weak_count"), py::is_method(cls), "Rack's WeakBase::getWeakCount: the WeakPtrs on this widget.");
		cls.attr("weak_handle") = py::module_::import("builtins").attr("property")(py::cpp_function([](py::object self) -> py::object {
			runtime::requireMainThread("using a widget");
			auto* w = argument(self);
			if (!w->weakHandle)
				return py::none();
			WeakHandleView view;
			view.keep.set(w);
			return py::cast(view);
		}), py::none(), py::none(), "Rack's WeakBase::weakHandle: the handle its WeakPtrs share, or None.");
	}
}

}  // namespace widgets
}  // namespace rackpy
