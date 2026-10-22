// SPDX-License-Identifier: GPL-3.0-or-later
// pybind11 variant of the RP-04 spike bindings.
#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include "host_core.hpp"

namespace py = pybind11;
using spike::Handle;

namespace {

struct Kernel {
	virtual ~Kernel() = default;
	virtual float configure(const Handle& handle) = 0;
};

struct PyKernel : Kernel, py::trampoline_self_life_support {
	float configure(const Handle& handle) override {
		PYBIND11_OVERRIDE_PURE(float, Kernel, configure, handle);
	}
};

/** Python subclasses of Widget. Rack owns attached children and frees them with `delete`. */
struct PyWidget : rack::widget::Widget, py::trampoline_self_life_support {
	void onAdd(const AddEvent& e) override {
		py::gil_scoped_acquire gil;
		if (py::function f = py::get_override(static_cast<const rack::widget::Widget*>(this), "on_add")) {
			f();
			return;
		}
		rack::widget::Widget::onAdd(e);
	}
	void onRemove(const RemoveEvent& e) override {
		py::gil_scoped_acquire gil;
		if (py::function f = py::get_override(static_cast<const rack::widget::Widget*>(this), "on_remove")) {
			f();
			return;
		}
		rack::widget::Widget::onRemove(e);
	}
};

struct Snapshot {
	std::vector<float> data;
};

std::shared_ptr<Kernel> kernel;

}  // namespace

PYBIND11_EMBEDDED_MODULE(_rackpy_spike, m) {
	py::register_exception_translator([](std::exception_ptr p) {
		try {
			if (p)
				std::rethrow_exception(p);
		}
		catch (const spike::StaleHandle& e) {
			PyErr_Format(PyExc_ReferenceError, "module %lld has been deleted", (long long) e.moduleId);
		}
	});

	m.def("log", [](const std::string& msg) { INFO("RACKPY_SPIKE py: %s", msg.c_str()); });

	py::class_<rack::math::Vec>(m, "Vec")
		.def(py::init<float, float>(), py::arg("x") = 0.f, py::arg("y") = 0.f)
		.def_readwrite("x", &rack::math::Vec::x)
		.def_readwrite("y", &rack::math::Vec::y)
		.def("__add__", [](const rack::math::Vec& a, const rack::math::Vec& b) { return a.plus(b); })
		.def("__repr__", [](const rack::math::Vec& v) { return rack::string::f("Vec(%g, %g)", v.x, v.y); });

	py::class_<Snapshot>(m, "Snapshot", py::buffer_protocol())
		.def_buffer([](Snapshot& s) { return py::buffer_info(s.data.data(), (py::ssize_t) s.data.size()); });

	py::class_<Handle>(m, "ModuleHandle")
		.def_property_readonly("id", &Handle::id)
		.def_property_readonly("alive", &Handle::alive)
		.def_property("gain", &Handle::gain, &Handle::setGain)
		.def_property_readonly("processed", &Handle::processed)
		.def("snapshot", [](const Handle& h) { return Snapshot{h.snapshot()}; });

	py::classh<Kernel, PyKernel>(m, "Kernel").def(py::init<>()).def("configure", &Kernel::configure);

	m.def("set_kernel", [](std::shared_ptr<Kernel> k) { kernel = std::move(k); });

	py::classh<rack::widget::Widget, PyWidget>(m, "Widget").def(py::init<>());

	m.def("storage_inside_python_object", [](py::object o) {
		auto p = reinterpret_cast<uintptr_t>(o.cast<rack::widget::Widget*>());
		auto base = reinterpret_cast<uintptr_t>(o.ptr());
		return p >= base && p < base + (uintptr_t) Py_TYPE(o.ptr())->tp_basicsize;
	});

	// Transfers ownership to Rack's widget tree, which will `delete` the child.
	m.def("attach", [](std::unique_ptr<rack::widget::Widget> child) {
		rack::widget::Widget* parent = spike::currentParent();
		if (!parent)
			throw std::runtime_error("attach() is only valid inside Kernel.configure()");
		parent->addChild(child.release());
	});
}

namespace spike {

namespace {

bool registerInittab() {
	// PYBIND11_EMBEDDED_MODULE registered the module when this library was loaded.
	return true;
}

bool afterInit(const std::string& script, std::string& err) {
	try {
		py::module_::import(script.c_str());
		return true;
	}
	catch (const std::exception& e) {
		err = e.what();
		return false;
	}
}

void onWidgetCreated(const Handle& handle) {
	try {
		if (!kernel)
			return;
		float gain = kernel->configure(handle);
		handle.token->gain.store(gain);
	}
	catch (const std::exception& e) {
		WARN("RACKPY_SPIKE configure raised: %s", e.what());
	}
}

void beforeFinalize() {
	try {
		py::module_::import("spike_plugin").attr("on_shutdown")();
	}
	catch (const std::exception& e) {
		WARN("RACKPY_SPIKE on_shutdown raised: %s", e.what());
	}
	kernel.reset();
}

const BindingOps ops = {"pybind11 " TOSTRING(PYBIND11_VERSION_MAJOR) "." TOSTRING(PYBIND11_VERSION_MINOR) "." TOSTRING(PYBIND11_VERSION_PATCH),
	registerInittab, afterInit, onWidgetCreated, beforeFinalize};

}  // namespace

const BindingOps& bindingOps() {
	return ops;
}

}  // namespace spike
