// SPDX-License-Identifier: GPL-3.0-or-later
// Family 3 adapter: the generated registry calls bind_plugin(). rack::plugin
// becomes rackpy.raw.plugin: plugins, models and createModel for Python module
// factories (RP-16A). The native models are in native/plugin/.
#include <pybind11/stl.h>

#include <rack.hpp>

#include "plugin.hpp"
#include "../plugin/model.hpp"
#include "engine/module.hpp"
#include "rackpy/runtime/errors.hpp"
#include "rackpy/runtime/interpreter.hpp"
#include "rackpy/runtime/json.hpp"
#include "rackpy/runtime/python.hpp"
#include "rackpy/runtime/threads.hpp"

namespace rackpy {
namespace bindings {

namespace py = pybind11;
using RackPlugin = rack::plugin::Plugin;
using RackModel = rack::plugin::Model;

namespace {

/** Python's module factory for one model. Rack calls it on its main thread;
an error is contained (Rack then gets a placeholder module). */
struct PyModuleFactory : plugin::ModuleFactory {
	runtime::PyRef fn;
	rack::engine::Module* create(RackModel& model) noexcept override {
		rack::engine::Module* result = nullptr;
		runtime::callPython("Model.create_module", [&] {
			if (!fn)
				throw std::runtime_error("the plugin's Python factories have been released");
			result = createForRack(fn.get(), model);
		});
		return result;
	}
	void release() noexcept override { fn.reset(); }
};

/** A Python-made model that has not been added yet belongs to its handle. */
struct Detached {
	std::unique_ptr<plugin::Model> owned;
};

struct PluginHandle {
	RackPlugin* plugin;
};

struct ModelHandle {
	RackModel* model;
	std::shared_ptr<Detached> detached;
};

bool pluginAlive(const RackPlugin* p) {
	if (p && p == plugin::instance())
		return true;
	return std::find(rack::plugin::plugins.begin(), rack::plugin::plugins.end(), p) != rack::plugin::plugins.end();
}

bool modelAlive(const RackModel* m) {
	for (const auto& s : plugin::staged())
		if (s.get() == m)
			return true;
	if (auto* p = plugin::instance())
		if (std::find(p->models.begin(), p->models.end(), m) != p->models.end())
			return true;
	for (auto* p : rack::plugin::plugins)
		if (std::find(p->models.begin(), p->models.end(), m) != p->models.end())
			return true;
	return false;
}

RackPlugin& checked(const PluginHandle& h) {
	runtime::requireMainThread("using a Rack plugin");
	if (!pluginAlive(h.plugin))
		throw runtime::StaleHandle("this plugin is no longer loaded");
	return *h.plugin;
}

RackModel& checked(const ModelHandle& h) {
	runtime::requireMainThread("using a Rack model");
	if (h.detached && h.detached->owned)
		return *h.model;
	if (!modelAlive(h.model))
		throw runtime::StaleHandle("this model is no longer registered (its plugin's registration failed or it was unloaded)");
	return *h.model;
}

py::object pluginObject(RackPlugin* p) {
	if (!p)
		return py::none();
	return runtime::identity(p, 0, [&] { return py::cast(PluginHandle{p}); });
}

py::object modelObject(RackModel* m) {
	if (!m)
		return py::none();
	return runtime::identity(m, 0, [&] { return py::cast(ModelHandle{m, nullptr}); });
}

/** Only this plugin's own models may change, as only a plugin's own code changes them in C++. */
RackModel& ownModel(const ModelHandle& h, const char* field) {
	RackModel& m = checked(h);
	if (m.plugin && m.plugin != plugin::instance())
		throw runtime::Ownership(std::string("Model.") + field + " of another plugin's model is read-only");
	return m;
}

RackModel& addedModel(const ModelHandle& h, const char* what) {
	RackModel& m = checked(h);
	if (!m.plugin)
		throw runtime::Ownership(std::string(what) + " needs the model's plugin: add the model to a plugin first");
	return m;
}

template <typename Field>
void modelField(py::class_<ModelHandle>& cls, const char* name, Field RackModel::*field, const char* doc) {
	cls.def_property(name, [field](const ModelHandle& h) { return checked(h).*field; },
		[name, field](const ModelHandle& h, Field value) { ownModel(h, name).*field = std::move(value); }, doc);
}

void addModel(const PluginHandle& h, ModelHandle& m) {
	RackPlugin& p = checked(h);
	if (&p != plugin::instance())
		throw runtime::Ownership("models can be added only to this plugin (rackpy.raw.plugin.plugin_instance())");
	if (!m.detached || !m.detached->owned)
		throw runtime::Ownership("this model has already been added to a plugin");
	try {
		plugin::stage(m.detached->owned);
	}
	catch (const std::logic_error& e) {
		if (dynamic_cast<const std::invalid_argument*>(&e))
			throw;
		throw runtime::Ownership(e.what());
	}
}

}  // namespace

void bind_plugin(py::module_ raw) {
	py::module_ mod = raw.def_submodule("plugin", "rack::plugin: plugins, models and createModel (RP-16A).");

	py::class_<PluginHandle>(mod, "Plugin", py::is_final(),
		"A loaded Rack plugin (rack::plugin::Plugin). Rack owns it for its whole run; fields come from its plugin.json.")
		.def_property_readonly("alive", [](const PluginHandle& h) {
			runtime::requireMainThread("using a Rack plugin");
			return pluginAlive(h.plugin);
		})
		.def_property_readonly("slug", [](const PluginHandle& h) { return checked(h).slug; })
		.def_property_readonly("name", [](const PluginHandle& h) { return checked(h).name; })
		.def_property_readonly("version", [](const PluginHandle& h) { return checked(h).version; })
		.def_property_readonly("license", [](const PluginHandle& h) { return checked(h).license; })
		.def_property_readonly("brand", [](const PluginHandle& h) { return checked(h).brand; })
		.def_property_readonly("description", [](const PluginHandle& h) { return checked(h).description; })
		.def_property_readonly("author", [](const PluginHandle& h) { return checked(h).author; })
		.def_property_readonly("author_email", [](const PluginHandle& h) { return checked(h).authorEmail; })
		.def_property_readonly("author_url", [](const PluginHandle& h) { return checked(h).authorUrl; })
		.def_property_readonly("plugin_url", [](const PluginHandle& h) { return checked(h).pluginUrl; })
		.def_property_readonly("manual_url", [](const PluginHandle& h) { return checked(h).manualUrl; })
		.def_property_readonly("source_url", [](const PluginHandle& h) { return checked(h).sourceUrl; })
		.def_property_readonly("donate_url", [](const PluginHandle& h) { return checked(h).donateUrl; })
		.def_property_readonly("changelog_url", [](const PluginHandle& h) { return checked(h).changelogUrl; })
		.def_property_readonly("path", [](const PluginHandle& h) { return checked(h).path; })
		.def_property_readonly("modified_timestamp", [](const PluginHandle& h) { return checked(h).modifiedTimestamp; })
		.def_property_readonly("models", [](const PluginHandle& h) {
			RackPlugin& p = checked(h);
			py::list out;
			for (auto* m : p.models)
				out.append(modelObject(m));
			if (&p == plugin::instance())
				for (const auto& m : plugin::staged())
					out.append(modelObject(m.get()));
			return py::tuple(out);
		}, "A snapshot of the plugin's models; while this plugin initializes, also the ones its entry module added.")
		.def("add_model", &addModel, py::arg("model"),
			"Rack's Plugin::addModel, for this plugin only and only while it initializes. The models must match "
			"plugin.json's \"modules\" when initialization ends, or none is added.")
		.def("get_model", [](const PluginHandle& h, const std::string& slug) {
			RackPlugin& p = checked(h);
			if (&p == plugin::instance())
				for (const auto& m : plugin::staged())
					if (m->slug == slug)
						return modelObject(m.get());
			return modelObject(p.getModel(slug));
		}, py::arg("slug"), "The model with this slug, or None.")
		.def("get_brand", [](const PluginHandle& h) { return checked(h).getBrand(); });

	auto model = py::class_<ModelHandle>(mod, "Model", py::is_final(),
		"A Rack model (rack::plugin::Model). Make one with create_model(); Rack owns it once it is added.");
	model.def_property_readonly("alive", [](const ModelHandle& h) {
			runtime::requireMainThread("using a Rack model");
			return (h.detached && h.detached->owned) || modelAlive(h.model);
		})
		.def_property("slug", [](const ModelHandle& h) { return checked(h).slug; },
			[](const ModelHandle& h, std::string slug) {
				RackModel& m = checked(h);
				if (!h.detached || !h.detached->owned)
					throw runtime::Ownership("a model's slug cannot change once it is added: Rack and patches look models up by it");
				m.slug = std::move(slug);
			}, "Writable only before the model is added.")
		.def_property_readonly("plugin", [](const ModelHandle& h) { return pluginObject(checked(h).plugin); });
	modelField(model, "name", &RackModel::name, "From plugin.json after initialization; this plugin's models only for writes.");
	modelField(model, "description", &RackModel::description, "From plugin.json after initialization.");
	modelField(model, "manual_url", &RackModel::manualUrl, "From plugin.json after initialization.");
	modelField(model, "modular_grid_url", &RackModel::modularGridUrl, "From plugin.json after initialization.");
	modelField(model, "hidden", &RackModel::hidden, "From plugin.json after initialization: hidden from the browser.");
	modelField(model, "tag_ids", &RackModel::tagIds, "Tag indices (rack::tag), from plugin.json after initialization; a copy.");
	model.def("get_full_name", [](const ModelHandle& h) { return addedModel(h, "get_full_name").getFullName(); })
		.def("get_factory_preset_directory", [](const ModelHandle& h) {
			return addedModel(h, "get_factory_preset_directory").getFactoryPresetDirectory();
		})
		.def("get_user_preset_directory", [](const ModelHandle& h) {
			return addedModel(h, "get_user_preset_directory").getUserPresetDirectory();
		})
		.def("get_manual_url", [](const ModelHandle& h) { return addedModel(h, "get_manual_url").getManualUrl(); })
		.def("is_favorite", [](const ModelHandle& h) { return addedModel(h, "is_favorite").isFavorite(); })
		.def("set_favorite", [](const ModelHandle& h, bool favorite) { addedModel(h, "set_favorite").setFavorite(favorite); },
			py::arg("favorite"));

	mod.def("create_model", [](std::string slug, py::object moduleFactory, int hp) {
		runtime::requireMainThread("creating a Rack model");
		if (!PyCallable_Check(moduleFactory.ptr()))
			throw py::type_error("module_factory must be callable: it is Module.create's constructor callback");
		if (hp < 1 || hp > 128)
			throw py::value_error("hp must be between 1 and 128");
		auto detached = std::make_shared<Detached>();
		detached->owned = std::make_unique<plugin::Model>();
		auto factory = std::make_unique<PyModuleFactory>();
		factory->fn = runtime::PyRef(moduleFactory);
		detached->owned->factory = std::move(factory);
		detached->owned->slug = std::move(slug);
		detached->owned->hp = hp;
		RackModel* m = detached->owned.get();
		return runtime::identity(m, 0, [&] { return py::cast(ModelHandle{m, detached}); });
	}, py::arg("slug"), py::arg("module_factory"), py::kw_only(), py::arg("hp") = 4,
		"Rack's createModel<TModule, TModuleWidget>: Rack calls module_factory(module), Module.create's constructor "
		"callback, whenever it creates the model's module. Until Python module widgets exist (RP-28) the module "
		"gets a plain panel hp wide.");
	mod.def("plugin_instance", [] {
		runtime::requireMainThread("using a Rack plugin");
		return pluginObject(plugin::instance());
	}, "This plugin (C++ plugins keep it as pluginInstance).");
	mod.def("is_slug_valid", &rack::plugin::isSlugValid, py::arg("slug"));
	mod.def("normalize_slug", &rack::plugin::normalizeSlug, py::arg("slug"));
	mod.def("get_plugin", [](const std::string& slug) {
		runtime::requireMainThread("finding a Rack plugin");
		return pluginObject(rack::plugin::getPlugin(slug));
	}, py::arg("plugin_slug"), "A loaded plugin, or None. While this plugin initializes, it is not loaded yet.");
	mod.def("get_plugin_fallback", [](const std::string& slug) {
		runtime::requireMainThread("finding a Rack plugin");
		return pluginObject(rack::plugin::getPluginFallback(slug));
	}, py::arg("plugin_slug"), "As get_plugin, also following Rack's renamed-plugin fallbacks.");
	mod.def("get_model", [](const std::string& pluginSlug, const std::string& modelSlug) {
		runtime::requireMainThread("finding a Rack model");
		return modelObject(rack::plugin::getModel(pluginSlug, modelSlug));
	}, py::arg("plugin_slug"), py::arg("model_slug"));
	mod.def("get_model_fallback", [](const std::string& pluginSlug, const std::string& modelSlug) {
		runtime::requireMainThread("finding a Rack model");
		return modelObject(rack::plugin::getModelFallback(pluginSlug, modelSlug));
	}, py::arg("plugin_slug"), py::arg("model_slug"));
	mod.def("model_from_json", [](py::handle moduleJson) {
		runtime::requireMainThread("finding a Rack model");
		std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(moduleJson), json_decref);
		return modelObject(rack::plugin::modelFromJson(json.get()));
	}, py::arg("module_json"), "The model a module's JSON names; Rack's Exception if it is not loaded.");

	runtime::onFinalize([] { plugin::releaseFactories(); });
}

void discardModels() {
	if (!runtime::interpreterAlive()) {
		plugin::discard();  // nothing staged can hold Python objects then
		return;
	}
	py::gil_scoped_acquire gil;
	plugin::discard();
}

std::string commitModels(RackPlugin* p, const std::string& manifestPath) {
	py::gil_scoped_acquire gil;
	return plugin::commit(p, manifestPath);
}

}  // namespace bindings
}  // namespace rackpy
