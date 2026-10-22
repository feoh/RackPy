// SPDX-License-Identifier: GPL-3.0-or-later
// nanobind variant of the RP-04 spike bindings.
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/trampoline.h>

#include "host_core.hpp"

namespace nb = nanobind;
using spike::Handle;

namespace {

struct Kernel {
	virtual ~Kernel() = default;
	virtual float configure(const Handle& handle) = 0;
};

struct PyKernel : Kernel {
	NB_TRAMPOLINE(Kernel);
	float configure(const Handle& handle) override {
		NB_OVERRIDE_PURE(configure, handle);
	}
};

/** Python subclasses of Widget, mirroring the pybind11 variant. */
struct PyWidget : rack::widget::Widget {
	NB_TRAMPOLINE(rack::widget::Widget);
	void onAdd(const AddEvent& e) override {
		nb::gil_scoped_acquire gil;
		nb::detail::ticket t(nb_trampoline, "on_add", nb::detail::str_hash("on_add"), false);
		if (t.key.is_valid()) {
			nb_trampoline.base().attr(t.key)();
			return;
		}
		rack::widget::Widget::onAdd(e);
	}
	void onRemove(const RemoveEvent& e) override {
		nb::gil_scoped_acquire gil;
		nb::detail::ticket t(nb_trampoline, "on_remove", nb::detail::str_hash("on_remove"), false);
		if (t.key.is_valid()) {
			nb_trampoline.base().attr(t.key)();
			return;
		}
		rack::widget::Widget::onRemove(e);
	}
};

std::shared_ptr<Kernel> kernel;

using FloatView = nb::ndarray<float, nb::shape<-1>, nb::memview>;

}  // namespace

NB_MODULE(_rackpy_spike, m) {
	nb::register_exception_translator([](const std::exception_ptr& p, void*) {
		try {
			std::rethrow_exception(p);
		}
		catch (const spike::StaleHandle& e) {
			PyErr_Format(PyExc_ReferenceError, "module %lld has been deleted", (long long) e.moduleId);
		}
	});

	m.def("log", [](const std::string& msg) { INFO("RACKPY_SPIKE py: %s", msg.c_str()); });

	nb::class_<rack::math::Vec>(m, "Vec")
		.def(nb::init<float, float>(), nb::arg("x") = 0.f, nb::arg("y") = 0.f)
		.def_rw("x", &rack::math::Vec::x)
		.def_rw("y", &rack::math::Vec::y)
		.def("__add__", [](const rack::math::Vec& a, const rack::math::Vec& b) { return a.plus(b); })
		.def("__repr__", [](const rack::math::Vec& v) { return rack::string::f("Vec(%g, %g)", v.x, v.y); });

	nb::class_<Handle>(m, "ModuleHandle")
		.def_prop_ro("id", &Handle::id)
		.def_prop_ro("alive", &Handle::alive)
		.def_prop_rw("gain", &Handle::gain, &Handle::setGain)
		.def_prop_ro("processed", &Handle::processed)
		.def("snapshot", [](const Handle& h) {
			auto* data = new std::vector<float>(h.snapshot());
			nb::capsule owner(data, [](void* p) noexcept { delete static_cast<std::vector<float>*>(p); });
			return FloatView(data->data(), {data->size()}, owner);
		});

	nb::class_<Kernel, PyKernel>(m, "Kernel").def(nb::init<>()).def("configure", &Kernel::configure);

	m.def("set_kernel", [](std::shared_ptr<Kernel> k) { kernel = std::move(k); });

	nb::class_<rack::widget::Widget, PyWidget>(m, "Widget").def(nb::init<>());

	m.def("storage_inside_python_object", [](nb::handle o) {
		auto p = reinterpret_cast<uintptr_t>(nb::inst_ptr<rack::widget::Widget>(o));
		auto base = reinterpret_cast<uintptr_t>(o.ptr());
		return p >= base && p < base + (uintptr_t) Py_TYPE(o.ptr())->tp_basicsize;
	});

	// Same signature as the pybind11 variant: Rack frees attached children with `delete`.
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
	return PyImport_AppendInittab("_rackpy_spike", PyInit__rackpy_spike) == 0;
}

bool afterInit(const std::string& script, std::string& err) {
	try {
		nb::module_::import_(script.c_str());
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
		nb::module_::import_("spike_plugin").attr("on_shutdown")();
	}
	catch (const std::exception& e) {
		WARN("RACKPY_SPIKE on_shutdown raised: %s", e.what());
	}
	kernel.reset();
}

const BindingOps ops = {"nanobind " TOSTRING(NB_VERSION_MAJOR) "." TOSTRING(NB_VERSION_MINOR) "." TOSTRING(NB_VERSION_PATCH),
	registerInittab, afterInit, onWidgetCreated, beforeFinalize};

}  // namespace

const BindingOps& bindingOps() {
	return ops;
}

}  // namespace spike
