// SPDX-License-Identifier: GPL-3.0-or-later
// RP-14A: scoped construction and checked, non-resizable component views.
// No Module virtual calls dispatch to Python. The sole Python call happens
// synchronously during construction on the main thread, before publication.
#include "module.hpp"
#include "components.hpp"

#include <pybind11/stl.h>

#include <vector>

namespace rackpy {
namespace bindings {

namespace {

enum class Kind { Param, Input, Output, Light };

int count(const ModuleRef& ref, Kind kind) {
	module::Module& m = ref.get();
	switch (kind) {
		case Kind::Param: return m.getNumParams();
		case Kind::Input: return m.getNumInputs();
		case Kind::Output: return m.getNumOutputs();
		case Kind::Light: return m.getNumLights();
	}
	throw std::logic_error("unknown component kind");
}

py::object component(const ModuleRef& ref, Kind kind, int i) {
	module::Module& m = ref.get();
	module::Construction::index(i, count(ref, kind));
	switch (kind) {
		case Kind::Param: return borrowParam(&m.getParam(i), m.token);
		case Kind::Input: return borrowInput(&m.getInput(i), m.token);
		case Kind::Output: return borrowOutput(&m.getOutput(i), m.token);
		case Kind::Light: return borrowLight(&m.getLight(i), m.token);
	}
	throw std::logic_error("unknown component kind");
}

/** Collections do not keep a module alive. Individual component views have
the module's ObjectToken (RP-13), not its owning Construction. */
struct Components {
	std::weak_ptr<module::Construction> state;
	Kind kind;
	ModuleRef ref() const {
		runtime::requireMainThread("using a module component collection");
		auto held = state.lock();
		if (!held)
			throw runtime::StaleHandle("this component collection belonged to a deleted module");
		ModuleRef r{held};
		r.get();
		return r;
	}
	int size() const { return count(ref(), kind); }
	py::object item(int i) const {
		ModuleRef r = ref();
		if (i < 0)
			i += count(r, kind);
		return component(r, kind, i);
	}
};

py::object create(py::object configure) {
	runtime::requireMainThread("creating a RackPy module");
	if (!configure.is_none() && !PyCallable_Check(configure.ptr()))
		throw py::type_error("configure must be callable or None");
	auto state = std::make_shared<module::Construction>();
	py::object result = py::cast(ModuleRef{state});
	try {
		if (!configure.is_none())
			configure(result);
		state->seal();
	}
	catch (...) {
		state->abort();
		throw;
	}
	return result;
}

}  // namespace

void bindModule(py::module_ engine) {
	py::class_<Components>(engine, "Components", "A checked, fixed-size view; it does not keep its module alive.")
		.def("__len__", &Components::size)
		.def("__getitem__", &Components::item)
		.def("__iter__", [](const Components& c) {
			int n = c.size();
			py::tuple items(n);
			for (int i = 0; i < n; i++)
				items[i] = c.item(i);
			return items.attr("__iter__")();
		});

	auto cls = py::class_<ModuleRef>(engine, "Module",
		"An owned native Rack module, built with Module.create(configure). Not yet registerable as a plugin model.");
	py::class_<rack::engine::Module::BypassRoute>(cls, "BypassRoute", "A route value; module.bypass_routes returns copies.")
		.def(py::init<>())
		.def_readwrite("input_id", &rack::engine::Module::BypassRoute::inputId)
		.def_readwrite("output_id", &rack::engine::Module::BypassRoute::outputId);

	cls.def_static("create", &create, py::arg("configure") = py::none())
		.def("close", [](const ModuleRef& r) { r.state->close(); },
			"Delete the owned native module, invalidating all component views. Idempotent.")
		.def_property_readonly("alive", [](const ModuleRef& r) { return r.state->object != nullptr; })
		.def("config", [](const ModuleRef& r, int p, int i, int o, int l) { r.state->config(p, i, o, l); },
			py::arg("num_params"), py::arg("num_inputs"), py::arg("num_outputs"), py::arg("num_lights") = 0)
		.def("config_bypass", [](const ModuleRef& r, int i, int o) { r.state->configBypass(i, o); },
			py::arg("input_id"), py::arg("output_id"))
		.def_property_readonly("bypass_routes", [](const ModuleRef& r) {
			py::tuple result(r.get().bypassRoutes.size());
			size_t i = 0;
			for (const auto& route : r.get().bypassRoutes)
				result[i++] = py::cast(route, py::return_value_policy::copy);
			return result;
		}, "Copied route values, not a mutable alias of Rack's vector.");

	struct Group { Kind kind; const char* field; const char* count; const char* get; };
	for (const auto& g : std::vector<Group>{
		{Kind::Param, "params", "get_num_params", "get_param"},
		{Kind::Input, "inputs", "get_num_inputs", "get_input"},
		{Kind::Output, "outputs", "get_num_outputs", "get_output"},
		{Kind::Light, "lights", "get_num_lights", "get_light"}
	}) {
		cls.def(g.count, [g](const ModuleRef& r) { return count(r, g.kind); });
		cls.def(g.get, [g](const ModuleRef& r, int i) { return component(r, g.kind, i); }, py::arg("index"));
		cls.def_property_readonly(g.field, [g](const ModuleRef& r) {
			r.get();
			return Components{r.state, g.kind};
		});
	}
}

}  // namespace bindings
}  // namespace rackpy
