// SPDX-License-Identifier: GPL-3.0-or-later
// RP-14A/B1: scoped construction, components and built-in metadata views.
// No Module virtual calls dispatch to Python. The sole Python call happens
// synchronously during construction on the main thread, before publication.
#include "module.hpp"
#include "components.hpp"
#include "quantity.hpp"

#include <pybind11/stl.h>

#include <vector>

#include "rackpy/runtime/python.hpp"

namespace rackpy {
namespace bindings {

py::object moduleObject(const std::shared_ptr<module::Construction>& state) {
	auto& m = state->get();
	return runtime::identity(&m, m.token->generation, [&] { return py::cast(ModuleRef{state}); });
}

namespace {

enum class Kind { Param, Input, Output, Light, ParamQuantity, InputInfo, OutputInfo, LightInfo };

/** A metadata view never owns the Module or the underlying metadata. Its
slot generation dies on replacement, independently of the Module lifetime. */
template <typename Info>
struct InfoView {
	Info* ptr;
	std::shared_ptr<runtime::ObjectToken> token;
	std::weak_ptr<module::Construction> state;
	Info& get() const {
		runtime::requireMainThread("using module metadata");
		if (!token->alive())
			throw runtime::StaleHandle("this metadata was replaced or its module was deleted");
		return *ptr;
	}
	py::object owner() const {
		get();
		auto held = state.lock();
		if (!held)
			throw runtime::StaleHandle("this metadata belonged to a deleted module");
		return moduleObject(held);
	}
};

using PortInfoView = InfoView<rack::engine::PortInfo>;
using LightInfoView = InfoView<rack::engine::LightInfo>;

template <typename Info>
py::object infoView(const ModuleRef& ref, Info* ptr, const std::shared_ptr<runtime::ObjectToken>& token) {
	if (!ptr)
		return py::none();
	return runtime::identity(ptr, token->generation, [&] { return py::cast(InfoView<Info>{ptr, token, ref.state}); });
}

template <typename Info>
auto bindInfoView(py::module_ engine, const char* name) {
	using View = InfoView<Info>;
	return py::class_<View>(engine, name, "Checked built-in metadata; not a standalone metadata class or a Python virtual trampoline.")
		.def_property_readonly("alive", [](const View& v) { return v.token->alive(); })
		.def_property_readonly("module", &View::owner)
		.def_property("name", [](const View& v) { return v.get().name; },
			[](const View& v, std::string name) { v.get().name = std::move(name); })
		.def_property("description", [](const View& v) { return v.get().description; },
			[](const View& v, std::string description) { v.get().description = std::move(description); })
		.def("get_name", [](const View& v) { return v.get().getName(); })
		.def("get_description", [](const View& v) { return v.get().getDescription(); });
}

int count(const ModuleRef& ref, Kind kind) {
	module::Module& m = ref.get();
	switch (kind) {
		case Kind::Param:
		case Kind::ParamQuantity: return m.getNumParams();
		case Kind::Input:
		case Kind::InputInfo: return m.getNumInputs();
		case Kind::Output:
		case Kind::OutputInfo: return m.getNumOutputs();
		case Kind::Light:
		case Kind::LightInfo: return m.getNumLights();
	}
	throw std::logic_error("unknown component kind");
}

py::object component(const ModuleRef& ref, Kind kind, int i) {
	module::Module& m = ref.get();
	module::Construction::index(i, count(ref, kind));
	switch (kind) {
		case Kind::Param: return borrowParam(&m.getParam(i), m.token);
		case Kind::ParamQuantity: return quantityView(ref, i);
		case Kind::Input: return borrowInput(&m.getInput(i), m.token);
		case Kind::Output: return borrowOutput(&m.getOutput(i), m.token);
		case Kind::Light: return borrowLight(&m.getLight(i), m.token);
		case Kind::InputInfo: return infoView(ref, m.getInputInfo(i), m.inputInfoTokens[i]);
		case Kind::OutputInfo: return infoView(ref, m.getOutputInfo(i), m.outputInfoTokens[i]);
		case Kind::LightInfo: return infoView(ref, m.getLightInfo(i), m.lightInfoTokens[i]);
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
	py::object result = moduleObject(state);
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
	bindQuantityViews(engine);
	bindInfoView<rack::engine::PortInfo>(engine, "PortInfoView")
		.def_property_readonly("port_id", [](const PortInfoView& v) { return v.get().portId; })
		.def_property_readonly("type", [](const PortInfoView& v) { return v.get().type; })
		.def("get_full_name", [](const PortInfoView& v) { return v.get().getFullName(); });
	bindInfoView<rack::engine::LightInfo>(engine, "LightInfoView")
		.def_property_readonly("light_id", [](const LightInfoView& v) { return v.get().lightId; });
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
		.def("config_param", [](const ModuleRef& r, int i, float min, float max, float def, std::string name,
			std::string unit, float base, float multiplier, float offset) {
			r.state->configQuantity(i, min, max, def, std::move(name), std::move(unit), base, multiplier, offset);
			return component(r, Kind::ParamQuantity, i);
		}, py::arg("param_id"), py::arg("min_value"), py::arg("max_value"), py::arg("default_value"),
			py::arg("name") = "", py::arg("unit") = "", py::arg("display_base") = 0.f,
			py::arg("display_multiplier") = 1.f, py::arg("display_offset") = 0.f,
			"Configure built-in ParamQuantity only; complete class-factory semantics remain planned.")
		.def("config_switch", [](const ModuleRef& r, int i, float min, float max, float def, std::string name,
			std::vector<std::string> labels) {
			r.state->configQuantity(i, min, max, def, std::move(name), "", 0.f, 1.f, 0.f, 1, std::move(labels));
			return component(r, Kind::ParamQuantity, i);
		}, py::arg("param_id"), py::arg("min_value"), py::arg("max_value"), py::arg("default_value"),
			py::arg("name") = "", py::arg("labels") = std::vector<std::string>{})
		.def("config_button", [](const ModuleRef& r, int i, std::string name) {
			r.state->configQuantity(i, 0.f, 1.f, 0.f, std::move(name), "", 0.f, 1.f, 0.f, 2);
			return component(r, Kind::ParamQuantity, i);
		}, py::arg("param_id"), py::arg("name") = "")
		.def("config_input", [](const ModuleRef& r, int i, std::string name) {
			r.state->configPortInfo(i, std::move(name), true);
			return component(r, Kind::InputInfo, i);
		}, py::arg("port_id"), py::arg("name") = "",
			"Configure built-in PortInfo only. Class-factory template semantics are not implemented yet.")
		.def("config_output", [](const ModuleRef& r, int i, std::string name) {
			r.state->configPortInfo(i, std::move(name), false);
			return component(r, Kind::OutputInfo, i);
		}, py::arg("port_id"), py::arg("name") = "")
		.def("config_light", [](const ModuleRef& r, int i, std::string name) {
			r.state->configLightInfo(i, std::move(name));
			return component(r, Kind::LightInfo, i);
		}, py::arg("light_id"), py::arg("name") = "")
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
		{Kind::Light, "lights", "get_num_lights", "get_light"},
		{Kind::ParamQuantity, "param_quantities", nullptr, "get_param_quantity"},
		{Kind::InputInfo, "input_infos", nullptr, "get_input_info"},
		{Kind::OutputInfo, "output_infos", nullptr, "get_output_info"},
		{Kind::LightInfo, "light_infos", nullptr, "get_light_info"}
	}) {
		if (g.count)
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
