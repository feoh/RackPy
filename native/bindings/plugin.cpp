// SPDX-License-Identifier: GPL-3.0-or-later
// Family 3 adapter: the generated registry calls bind_plugin(). rack::plugin
// becomes rackpy.raw.plugin: plugins, models and createModel for Python module
// factories (RP-16A). The native models are in native/plugin/.
#include <pybind11/stl.h>

#include <rack.hpp>

#include "plugin.hpp"
#include "../plugin/model.hpp"
#include "engine/module.hpp"
#include "widget/managed.hpp"
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

/** rack::plugin::plugins, read live. Rack appends to it while it loads plugins. */
struct PluginList {};

/** Plugin::handle, the plugin library's handle: identity and null checks only, never an address. */
struct LibraryHandle {
	const void* handle;
};

// Plugin-wide settings handlers (RP-15D).
runtime::PyRef settingsToJsonFn, settingsFromJsonFn;

// rackpy.plugin.on_destroy callbacks, run from destroy() (RP-16B).
std::vector<runtime::PyRef> destroyCallbacks;

void runDestroyCallbacks() {
	while (!destroyCallbacks.empty()) {
		runtime::PyRef callback = std::move(destroyCallbacks.back());
		destroyCallbacks.pop_back();
		runtime::callPython("plugin destroy callback", [&] { callback.get()(); });
	}
}

/** Rack's Model::fromJson assumes a well-formed plugin.json entry: a string
where it reads one. Check the JSON first, so a Python caller gets TypeError
rather than Rack dereferencing a null string. */
void checkModelJson(py::handle data) {
	if (!PyDict_Check(data.ptr()))
		throw py::type_error("Model.from_json takes a dict, as one of plugin.json's \"modules\" entries");
	auto json = py::reinterpret_borrow<py::dict>(data);
	for (const char* key : {"name", "description", "manualUrl", "modularGridUrl"})
		if (json.contains(key) && !py::isinstance<py::str>(json[key]))
			throw py::type_error(std::string("Model.from_json: \"") + key + "\" must be a str");
	if (json.contains("tags")) {
		py::object tags = json["tags"];
		if (!py::isinstance<py::list>(tags) && !py::isinstance<py::tuple>(tags))
			throw py::type_error("Model.from_json: \"tags\" must be a list of str");
		for (py::handle tag : tags)
			if (!py::isinstance<py::str>(tag))
				throw py::type_error("Model.from_json: \"tags\" must be a list of str");
	}
	for (const char* key : {"hidden", "disabled"})
		if (json.contains(key) && !py::isinstance<py::bool_>(json[key]))
			throw py::type_error(std::string("Model.from_json: \"") + key + "\" must be a bool");
}

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

py::object modelObjectInternal(RackModel* m) {
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


/** Plugin::fromJson and modulesFromJson belong to Rack's plugin loader, so
Python may call them only for this plugin while it initializes (owner
decision 2026-10-24). */
RackPlugin& initializingPlugin(const PluginHandle& h, const char* what) {
	RackPlugin& p = checked(h);
	if (&p != plugin::instance())
		throw runtime::Ownership(std::string(what) + " is Rack's plugin loader: only this plugin may call it");
	if (!plugin::registrationOpen())
		throw runtime::Ownership(std::string(what) + " is Rack's plugin loader: only while this plugin initializes");
	return p;
}

void requireStrings(const py::dict& json, std::initializer_list<const char*> keys, const char* what) {
	for (const char* key : keys)
		if (json.contains(key) && !py::isinstance<py::str>(json[key]))
			throw py::type_error(std::string(what) + ": \"" + key + "\" must be a str");
}

void pluginFromJson(const PluginHandle& h, py::handle data) {
	RackPlugin& p = initializingPlugin(h, "Plugin.from_json");
	if (!PyDict_Check(data.ptr()))
		throw py::type_error("Plugin.from_json takes a dict, as plugin.json");
	auto json = py::reinterpret_borrow<py::dict>(data);
	requireStrings(json, {"slug", "version", "name", "brand", "description", "author", "license", "authorEmail",
		"pluginUrl", "authorUrl", "manualUrl", "sourceUrl", "donateUrl", "changelogUrl"}, "Plugin.from_json");
	if (json.contains("slug") && json["slug"].cast<std::string>() != p.slug)
		throw py::value_error("Plugin.from_json cannot change the plugin's slug: Rack has already registered it");
	// Rack's fromJson sets fields one by one and may throw part-way: restore them if it does.
	std::vector<std::string RackPlugin::*> fields = {&RackPlugin::slug, &RackPlugin::version, &RackPlugin::name,
		&RackPlugin::brand, &RackPlugin::description, &RackPlugin::author, &RackPlugin::license,
		&RackPlugin::authorEmail, &RackPlugin::pluginUrl, &RackPlugin::authorUrl, &RackPlugin::manualUrl,
		&RackPlugin::sourceUrl, &RackPlugin::donateUrl, &RackPlugin::changelogUrl};
	std::vector<std::string> saved;
	for (auto field : fields)
		saved.push_back(p.*field);
	std::unique_ptr<json_t, decltype(&json_decref)> rootJ(runtime::toJson(data), json_decref);
	try {
		p.fromJson(rootJ.get());
	}
	catch (...) {
		for (size_t i = 0; i < fields.size(); i++)
			p.*fields[i] = saved[i];
		throw;
	}
}

void modulesFromJson(const PluginHandle& h, py::handle data) {
	RackPlugin& p = initializingPlugin(h, "Plugin.modules_from_json");
	if (!PyList_Check(data.ptr()) && !PyTuple_Check(data.ptr()))
		throw py::type_error("Plugin.modules_from_json takes a list, as plugin.json's \"modules\"");
	std::vector<std::string> listed;
	size_t index = 0;
	for (py::handle entry : data) {
		if (!PyDict_Check(entry.ptr()))
			throw py::type_error("Plugin.modules_from_json: every module entry must be a dict");
		auto json = py::reinterpret_borrow<py::dict>(entry);
		if (!json.contains("slug"))
			throw py::value_error("Plugin.modules_from_json: module entry #" + std::to_string(index) + " has no slug");
		requireStrings(json, {"slug"}, "Plugin.modules_from_json");
		checkModelJson(entry);
		std::string name = json.contains("name") ? json["name"].cast<std::string>() : "";
		std::string slug = json["slug"].cast<std::string>();
		if (!rack::plugin::isSlugValid(slug))
			throw py::value_error("Plugin.modules_from_json: module slug \"" + slug + "\" is invalid");
		if (name.empty())
			throw py::value_error("Plugin.modules_from_json: module " + slug + " has no name");
		listed.push_back(slug);
		index++;
	}
	std::string problem = plugin::matchModules(&p, listed, "these \"modules\"");
	if (!problem.empty())
		throw py::value_error(problem);
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
		.def_property_readonly("handle", [](const PluginHandle& h) -> py::object {
			const void* handle = checked(h).handle;
			if (!handle)
				return py::none();  // Core, which Rack does not load from a library
			return runtime::identity(handle, 0, [&] { return py::cast(LibraryHandle{handle}); });
		}, "The plugin library's handle: opaque, compared by identity; None for Core.")
		.def_property_readonly("models", [](const PluginHandle& h) {
			RackPlugin& p = checked(h);
			py::list out;
			for (auto* m : p.models)
				out.append(modelObjectInternal(m));
			if (&p == plugin::instance())
				for (const auto& m : plugin::staged())
					out.append(modelObjectInternal(m.get()));
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
						return modelObjectInternal(m.get());
			return modelObjectInternal(p.getModel(slug));
		}, py::arg("slug"), "The model with this slug, or None.")
		.def("get_brand", [](const PluginHandle& h) { return checked(h).getBrand(); })
		.def("from_json", &pluginFromJson, py::arg("data"),
			"Rack's Plugin::fromJson (plugin.json's plugin fields), for this plugin while it initializes. The slug "
			"cannot change; on Rack's Exception the fields are restored.")
		.def("modules_from_json", &modulesFromJson, py::arg("data"),
			"Rack's Plugin::modulesFromJson, which Rack runs after initialization: here a check that these "
			"\"modules\" entries match the models added so far (ValueError otherwise). Nothing changes.");

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
			py::arg("favorite"))
		.def("append_context_menu", [](const ModelHandle& h, py::object menu, bool inBrowser) {
			RackModel& m = addedModel(h, "append_context_menu");
			auto* target = dynamic_cast<rack::ui::Menu*>(widgets::argument(menu));
			if (!target)
				throw py::type_error("append_context_menu takes a rackpy.raw.ui.Menu");
			widgets::checkMutationOf(*target);
			m.appendContextMenu(target, inBrowser);
		}, py::arg("menu"), py::arg("in_browser") = false,
			"Rack's Model::appendContextMenu: Rack's own items for this model (manual, presets folder, favorite...).")
		.def("from_json", [](const ModelHandle& h, py::handle data) {
			checkModelJson(data);
			RackModel& m = addedModel(h, "from_json");
			ownModel(h, "from_json");
			std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(data), json_decref);
			m.fromJson(json.get());
		}, py::arg("data"),
			"Rack's Model::fromJson: name, description, tags, URLs and hidden from a plugin.json \"modules\" entry. "
			"This plugin's models only; Rack's Exception if the name ends up empty.");

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
		return modelObjectInternal(rack::plugin::getModel(pluginSlug, modelSlug));
	}, py::arg("plugin_slug"), py::arg("model_slug"));
	mod.def("get_model_fallback", [](const std::string& pluginSlug, const std::string& modelSlug) {
		runtime::requireMainThread("finding a Rack model");
		return modelObjectInternal(rack::plugin::getModelFallback(pluginSlug, modelSlug));
	}, py::arg("plugin_slug"), py::arg("model_slug"));
	mod.def("model_from_json", [](py::handle moduleJson) {
		runtime::requireMainThread("finding a Rack model");
		std::unique_ptr<json_t, decltype(&json_decref)> json(runtime::toJson(moduleJson), json_decref);
		return modelObjectInternal(rack::plugin::modelFromJson(json.get()));
	}, py::arg("module_json"), "The model a module's JSON names; Rack's Exception if it is not loaded.");

	py::class_<LibraryHandle>(mod, "LibraryHandle", py::is_final(),
		"A plugin library's handle (Plugin::handle). Opaque: identity only, never an address.");

	py::class_<PluginList>(mod, "PluginList", py::is_final(),
		"rack::plugin::plugins: the loaded plugins, read live and read-only. Rack adds to it while it loads plugins.")
		.def("__len__", [](const PluginList&) {
			runtime::requireMainThread("listing Rack's plugins");
			return rack::plugin::plugins.size();
		})
		.def("__getitem__", [](const PluginList&, py::ssize_t index) {
			runtime::requireMainThread("listing Rack's plugins");
			auto size = (py::ssize_t) rack::plugin::plugins.size();
			if (index < 0)
				index += size;
			if (index < 0 || index >= size)
				throw py::index_error("plugin index out of range");
			return pluginObject(rack::plugin::plugins[(size_t) index]);
		}, py::arg("index"))
		.def("__getitem__", [](const PluginList&, py::slice slice) {
			runtime::requireMainThread("listing Rack's plugins");
			py::list out;
			for (auto* p : rack::plugin::plugins)
				out.append(pluginObject(p));
			return py::object(py::tuple(out)[slice]);
		}, py::arg("index"))
		.def("__iter__", [](const PluginList&) {
			runtime::requireMainThread("listing Rack's plugins");
			py::list out;
			for (auto* p : rack::plugin::plugins)
				out.append(pluginObject(p));
			return py::iter(py::tuple(out));
		}, "Iterates the plugins loaded when iteration starts.")
		.def("__contains__", [](const PluginList&, py::handle value) {
			runtime::requireMainThread("listing Rack's plugins");
			if (!py::isinstance<PluginHandle>(value))
				return false;
			auto* p = value.cast<const PluginHandle&>().plugin;
			return std::find(rack::plugin::plugins.begin(), rack::plugin::plugins.end(), p) != rack::plugin::plugins.end();
		}, py::arg("value"));
	py::module_::import("collections.abc").attr("Sequence").attr("register")(mod.attr("PluginList"));
	mod.attr("plugins") = py::cast(PluginList{});
	mod.attr("pluginsPath") = py::str(rack::plugin::pluginsPath);

	mod.def("on_destroy", [](py::function callback) {
		runtime::requireMainThread("registering a destroy callback");
		destroyCallbacks.emplace_back(callback);
		return callback;
	}, py::arg("callback"),
		"Run callback when Rack calls the plugin's destroy(), as it exits: on the main thread, newest first, after "
		"every handle to a Rack object has gone stale. Errors are logged. Returns callback (a decorator).");

	mod.def("set_settings_handlers", [](py::object toJson, py::object fromJson) {
		runtime::requireMainThread("setting plugin settings handlers");
		for (py::handle f : {toJson, fromJson})
			if (!f.is_none() && !PyCallable_Check(f.ptr()))
				throw py::type_error("settings handlers must be callable or None");
		settingsToJsonFn = toJson.is_none() ? runtime::PyRef() : runtime::PyRef(toJson);
		settingsFromJsonFn = fromJson.is_none() ? runtime::PyRef() : runtime::PyRef(fromJson);
	}, py::arg("to_json") = py::none(), py::arg("from_json") = py::none(),
		"Rack's settingsToJson/settingsFromJson for this plugin: to_json() returns JSON-compatible data for "
		"settings.json (None keeps what is stored); from_json(data) receives it once, right after initialization, "
		"if settings.json has any. Set both from the entry module.");

	py::module_ asset = raw.def_submodule("asset", "rack::asset: paths to Rack's, the user's and a plugin's files (RP-16B).");
	asset.def("system", &rack::asset::system, py::arg("filename") = "", "A path in Rack's read-only system folder.");
	asset.def("user", &rack::asset::user, py::arg("filename") = "", "A path in Rack's readable and writable user folder.");
	asset.def("plugin", [](const PluginHandle& h, const std::string& filename) {
		return rack::asset::plugin(&checked(h), filename);
	}, py::arg("plugin"), py::arg("filename") = "", "A path in a plugin's read-only folder.");
	// Rack sets these before it loads plugins ("set these before calling init()"); they are fixed afterwards.
	asset.attr("systemDir") = py::str(rack::asset::systemDir);
	asset.attr("userDir") = py::str(rack::asset::userDir);
	asset.attr("oldUserDir") = py::str(rack::asset::oldUserDir);
	asset.attr("bundlePath") = py::str(rack::asset::bundlePath);

	runtime::onFinalize([] {
		runDestroyCallbacks();
		plugin::releaseFactories();
		settingsToJsonFn.reset();
		settingsFromJsonFn.reset();
	});
}

json_t* settingsToJson() noexcept {
	json_t* result = nullptr;
	runtime::callPython("plugin settings to_json", [&] {
		if (!settingsToJsonFn)
			return;
		py::object value = settingsToJsonFn.get()();
		if (!value.is_none())
			result = runtime::toJson(value);
	});
	return result;
}

void settingsFromJson(json_t* json) noexcept {
	runtime::callPython("plugin settings from_json", [&] {
		if (settingsFromJsonFn)
			settingsFromJsonFn.get()(runtime::fromJson(json));
	});
}

pybind11::object modelObject(RackModel* model) {
	runtime::requireMainThread("using a Rack model");
	return modelObjectInternal(model);
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
