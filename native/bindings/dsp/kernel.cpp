// SPDX-License-Identifier: GPL-3.0-or-later
#include "kernel.hpp"

#include <pybind11/stl.h>

#include <cmath>

#include "../../dsp/runtime/runtime.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;

namespace {

struct TypeHandle {
	const KernelType* type;
};

/** A kernel Python made. Before Module.set_kernel it owns the kernel; after,
the module does, and the handle reaches it through the module while that
kernel is the module's latest. */
struct KernelHandle {
	const KernelType* type;
	std::unique_ptr<dsp::Hosted> owned;
	dsp::Hosted* hosted = nullptr;
	std::shared_ptr<runtime::ObjectToken> hostedToken;
	module::Module* module = nullptr;
	std::shared_ptr<runtime::ObjectToken> moduleToken;
};

const char* kindName(Field::Kind kind) {
	switch (kind) {
		case Field::INPUT: return "input";
		case Field::OUTPUT: return "output";
		case Field::PARAM: return "param";
		case Field::LIGHT: return "light";
		case Field::NUMBER: return "number";
	}
	return "?";
}

double fieldValue(const KernelType& type, const Field& field, py::handle value) {
	std::string where = type.name + "(" + field.name + "=...)";
	if (PyBool_Check(value.ptr()))
		throw py::type_error(where + " takes a number, not a bool");
	if (field.kind == Field::NUMBER) {
		if (!PyLong_Check(value.ptr()) && !PyFloat_Check(value.ptr()))
			throw py::type_error(where + " takes an int or a float");
		double v = value.cast<double>();
		if (!std::isfinite(v))
			throw py::value_error(where + " must be finite");
		return v;
	}
	if (!PyLong_Check(value.ptr()))
		throw py::type_error(where + " takes a " + kindName(field.kind) + " index (an int; -1 for none if optional)");
	long long index = value.cast<long long>();
	if (index < -1 || index > (1 << 20))
		throw py::value_error(where + " is out of range");
	return (double) index;
}

std::string controlsText(const KernelType& type) {
	std::string out;
	for (const auto& name : type.controls)
		out += (out.empty() ? "" : ", ") + name;
	return out.empty() ? "none" : out;
}

int controlSlot(const KernelType& type, const std::string& name) {
	for (size_t i = 0; i < type.controls.size(); i++)
		if (type.controls[i] == name)
			return (int) i;
	throw py::key_error(type.name + " has no control " + name + " (controls: " + controlsText(type) + ")");
}

size_t fieldIndex(const KernelType& type, const std::string& name) {
	for (size_t i = 0; i < type.fields.size(); i++)
		if (type.fields[i].name == name)
			return i;
	throw std::logic_error("control " + name + " is not a field of " + type.name);
}

KernelHandle createKernel(const KernelType& type, py::kwargs kwargs) {
	runtime::requireMainThread("creating a kernel");
	std::vector<double> values;
	for (const Field& field : type.fields) {
		if (kwargs.contains(field.name.c_str()))
			values.push_back(fieldValue(type, field, kwargs[field.name.c_str()]));
		else if (field.required)
			throw py::type_error(type.name + "() needs " + field.name + " (" + kindName(field.kind) + ")");
		else
			values.push_back(field.defaultValue);
	}
	for (auto item : kwargs) {
		std::string name = item.first.cast<std::string>();
		bool known = false;
		for (const Field& field : type.fields)
			known = known || field.name == name;
		if (!known)
			throw py::type_error(type.name + "() got an unexpected field " + name);
	}
	KernelHandle handle;
	handle.type = &type;
	handle.owned = std::make_unique<dsp::Hosted>();
	handle.owned->type = &type;
	handle.owned->values = values;
	// A kernel library's create() runs its own code, with its own copy of the C++ runtime: turn
	// whatever it throws into ValueError here rather than relying on matching exception types.
	try {
		handle.owned->kernel = type.create(values);
	}
	catch (const std::exception& e) {
		throw py::value_error(e.what());
	}
	catch (...) {
		throw py::value_error(type.name + " refused these fields");
	}
	if (!handle.owned->kernel)
		throw py::value_error(type.name + " created no kernel");
	handle.hosted = handle.owned.get();
	handle.hostedToken = handle.owned->token;
	return handle;
}

/** The module and kernel an attached handle reaches; throws if either is gone or replaced. */
module::Module& attachedModule(const KernelHandle& k) {
	if (!k.moduleToken->alive() || !k.hostedToken->alive())
		throw runtime::StaleHandle("this kernel's module has been deleted, or the kernel replaced and reclaimed");
	if (k.module->kernels.latest() != k.hosted)
		throw runtime::StaleHandle("this kernel has been replaced on its module");
	return *k.module;
}

void setControl(KernelHandle& k, const std::string& name, py::handle value) {
	runtime::requireMainThread("changing a kernel control");  // the queue has a single producer
	int slot = controlSlot(*k.type, name);
	double v = fieldValue(*k.type, k.type->fields[fieldIndex(*k.type, name)], value);
	if (k.owned) {
		k.owned->kernel->setControl(slot, v);
	}
	else {
		module::Module& m = attachedModule(k);
		if (!m.kernels.set(k.hosted, slot, v, m.engineOwned.load(std::memory_order_acquire)))
			throw py::value_error(k.type->name + ": the control queue is full (" +
				std::to_string(dsp::CONTROL_CAPACITY) + " values the engine has not consumed); try again later");
	}
	k.hosted->values[fieldIndex(*k.type, name)] = v;  // alive: checked above
}

py::object typeObject(const KernelType* type) {
	return runtime::identity(type, 0, [&] { return py::cast(TypeHandle{type}); });
}

}  // namespace

void attachKernel(const ModuleRef& ref, py::handle kernel) {
	runtime::requireMainThread("setting a module's kernel");
	module::Module& m = ref.get();
	if (!py::isinstance<KernelHandle>(kernel))
		throw py::type_error("set_kernel takes a kernel, e.g. rackpy.dsp.kernel.Gain(input=0, output=0)");
	KernelHandle& k = kernel.cast<KernelHandle&>();
	if (!k.owned)
		throw runtime::Ownership("this kernel already belongs to a module; create another one");
	dsp::checkWiring(*k.type, k.owned->values, m);  // std::invalid_argument -> ValueError
	k.module = &m;
	k.moduleToken = m.token;
	dsp::Hosted* hosted = k.hosted;
	m.kernels.attach(std::move(k.owned), m.engineOwned.load(std::memory_order_acquire));
	// Module.kernel returns this same object while it is the module's kernel.
	runtime::identity(hosted, hosted->token->generation, [&] { return py::reinterpret_borrow<py::object>(kernel); });
}

py::object moduleKernel(const ModuleRef& ref) {
	runtime::requireMainThread("reading a module's kernel");
	module::Module& m = ref.get();
	m.kernels.reclaim();
	dsp::Hosted* hosted = m.kernels.latest();
	if (!hosted)
		return py::none();
	return runtime::identity(hosted, hosted->token->generation, [&] {
		KernelHandle k;
		k.type = hosted->type;
		k.hosted = hosted;
		k.hostedToken = hosted->token;
		k.module = &m;
		k.moduleToken = m.token;
		return py::cast(std::move(k));
	});
}

void bindKernels(py::module_ host) {
	dsp::registerBuiltins();
	py::module_ mod = host.def_submodule("dsp", "Native DSP kernels (RP-17A); use rackpy.dsp.kernel.");

	py::class_<TypeHandle>(mod, "KernelType", py::is_final(),
		"A kind of native kernel. Call it with its fields to make a Kernel for Module.set_kernel.")
		.def_property_readonly("name", [](const TypeHandle& t) { return t.type->name; })
		.def_property_readonly("doc", [](const TypeHandle& t) { return t.type->doc; })
		.def_property_readonly("fields", [](const TypeHandle& t) {
			py::list out;
			for (const Field& f : t.type->fields)
				out.append(py::make_tuple(f.name, kindName(f.kind), f.defaultValue, f.required, f.doc));
			return py::tuple(out);
		}, "(name, kind, default, required, doc) for each field; kind is input, output, param, light or number.")
		.def_property_readonly("controls", [](const TypeHandle& t) { return py::tuple(py::cast(t.type->controls)); })
		.def("__call__", [](const TypeHandle& t, py::kwargs kwargs) { return createKernel(*t.type, kwargs); })
		.def("__repr__", [](const TypeHandle& t) { return "<kernel type " + t.type->name + ">"; });

	py::class_<KernelHandle>(mod, "Kernel", py::is_final(),
		"A native kernel. Module.set_kernel hands it to a module; set() changes a control without running Python "
		"on the audio path.")
		.def_property_readonly("type", [](const KernelHandle& k) { return typeObject(k.type); })
		.def_property_readonly("attached", [](const KernelHandle& k) { return !k.owned; })
		.def_property_readonly("alive", [](const KernelHandle& k) {
			runtime::requireMainThread("checking a kernel");
			if (k.owned)
				return true;
			if (k.moduleToken->alive())
				k.module->kernels.reclaim();
			return k.moduleToken->alive() && k.hostedToken->alive() && k.module->kernels.latest() == k.hosted;
		}, "False once its module is deleted or another kernel replaced it.")
		.def_property_readonly("values", [](const KernelHandle& k) {
			runtime::requireMainThread("reading a kernel");
			if (!k.owned)
				attachedModule(k);
			py::dict out;
			for (size_t i = 0; i < k.type->fields.size(); i++) {
				const Field& f = k.type->fields[i];
				double v = k.hosted->values[i];
				if (f.kind == Field::NUMBER)
					out[f.name.c_str()] = v;
				else
					out[f.name.c_str()] = (long long) v;
			}
			return out;
		}, "The fields as created, with controls as last set from Python.")
		.def("set", &setControl, py::arg("name"), py::arg("value"),
			"Change a control. On a module Rack's engine runs, the value reaches the kernel at the next frame "
			"through a bounded queue; ValueError if it is full. Main thread.")
		.def("__repr__", [](const KernelHandle& k) {
			return "<kernel " + k.type->name + (k.owned ? " (not attached)" : "") + ">";
		});

	mod.def("kernel_types", [] {
		py::list out;
		for (const KernelType* type : dsp::types())
			out.append(typeObject(type));
		return py::tuple(out);
	});
	mod.def("kernel_type", [](const std::string& name) {
		const KernelType* type = dsp::findType(name);
		if (!type)
			throw py::key_error("no kernel type " + name);
		return typeObject(type);
	}, py::arg("name"));
	mod.attr("CONTROL_CAPACITY") = dsp::CONTROL_CAPACITY;
	mod.attr("KERNEL_ABI") = KERNEL_ABI;
}

}  // namespace bindings
}  // namespace rackpy
