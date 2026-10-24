# Plugins and models

`rackpy.raw.plugin` is `rack::plugin` and `rackpy.raw.asset` is
`rack::asset`. **Implemented (RP-16A, RP-16B), Linux x86_64
inside Rack Free 2.6.6**, for the records listed as implemented in the
[coverage report](coverage.md). The guide is [Register models](../how-to/plugins.md).

(plugin-plugins)=
## Plugins

`rackpy.raw.plugin.plugin_instance()` is this plugin, which C++ plugins keep
as `pluginInstance`. `get_plugin(slug)` and `get_plugin_fallback(slug)` (which
follows Rack's renamed-plugin table) find any loaded plugin, or return `None`.
While this plugin initializes, Rack has not finished loading it, so only
`plugin_instance()` finds it.

A `Plugin` has Rack's fields, read-only, from its `plugin.json`: `slug`,
`name`, `version`, `license`, `brand`, `description`, `author`,
`author_email`, `author_url`, `plugin_url`, `manual_url`, `source_url`,
`donate_url`, `changelog_url`, plus `path` (its directory) and
`modified_timestamp`. Rack reads them before `init()`, and plugin code has no
reason to change them. `handle` is the plugin library's handle as an opaque
`LibraryHandle`, compared by identity (never an address), or `None` for Core,
which is part of Rack. `models` is a tuple snapshot; `get_model(slug)` and
`get_brand()` are Rack's methods. `add_model(model)` is Rack's
`Plugin::addModel`, for this plugin only and only while it initializes; the
models are added to Rack when initialization ends, all or none
([plugin.json](../how-to/plugins.md#pluginjson-lists-the-same-models)).

(plugin-loader)=
## Rack's loader methods

`Plugin.from_json(data)` and `Plugin.modules_from_json(entries)` are Rack's
`Plugin::fromJson` and `Plugin::modulesFromJson`, which Rack's plugin loader
calls with `plugin.json`. RackPy allows them only for this plugin while it
initializes (`OwnershipError` otherwise). `from_json` sets the plugin fields
it finds, refuses to change the slug, and restores everything if Rack raises
(for example for a version that is not Rack 2's). `modules_from_json` does
not reorder or delete anything: Rack runs the real one after initialization,
so here it checks, with Rack's rules, that the entries match the models
added so far, and raises `ValueError` otherwise.

(plugin-models)=
## Models

`create_model(slug, module_factory, *, hp=4)` is Rack's
`createModel<TModule, TModuleWidget>`: Rack calls `module_factory(module)`,
Module.create's constructor callback, whenever it creates the model's module,
with errors contained (a [placeholder](../how-to/plugins.md#when-a-module-factory-fails)
replaces the module). `hp` sizes the plain panel each module gets until
Python module widgets exist (RP-28); until then the `TModuleWidget` half of
the template is unmapped, so the coverage report lists `createModel` as
planned. The new `Model` belongs to Python until
`add_model`.

A `Model` has Rack's fields. `slug` can change only before the model is
added: Rack and patches look models up by it. `plugin` is its plugin, or
`None` before it is added. `name`, `description`, `manual_url`,
`modular_grid_url`, `hidden` and `tag_ids` come from `plugin.json` when the
plugin finishes loading; this plugin may change its own models' values (as
its C++ could), and another plugin's are read-only (`OwnershipError`).
`tag_ids` returns a copy.

Rack's methods need the model's plugin: `get_full_name()` (brand and name),
`get_manual_url()`, `get_factory_preset_directory()`,
`get_user_preset_directory()`, `is_favorite()` and `set_favorite()`.
`get_model(plugin_slug, model_slug)`, `get_model_fallback(...)` and
`model_from_json(module_json)`, which raises Rack's `rackpy.raw.Exception`
for a model that is not loaded, find models. `is_slug_valid()` and
`normalize_slug()` are Rack's slug rules.

`from_json(entry)` is Rack's `Model::fromJson`, which sets those fields from
a `plugin.json` `"modules"` entry (unknown tags are dropped). It takes this
plugin's added models only; RackPy checks the entry's types first, because
Rack assumes strings where it reads them, and Rack raises its `Exception` if
the name ends up empty. A model created by `create_model` and never added is
deleted with its last Python reference; Rack deletes added models, and its
plugins, when it exits, after `destroy()`.

(plugin-registry)=
## The plugin list

`plugins` is Rack's list of loaded plugins (`rack::plugin::plugins`), read
live and read-only: `len()`, indexing, slicing and `in`; iterating walks the
plugins loaded when it starts. This plugin joins it only once it has loaded.
`pluginsPath` is Rack's plugin folder.

(plugin-assets)=
## Asset paths

`rackpy.raw.asset.system(filename="")`, `user(filename="")` and
`plugin(plugin, filename="")` are Rack's path functions: strings, joined onto
Rack's folders, nothing opened or owned. `systemDir`, `userDir`,
`oldUserDir` and `bundlePath` (empty except on macOS) are Rack's variables
as Rack set them before it loaded plugins; they do not change afterwards.
`rackpy.assets` returns the same paths as `pathlib.Path`, for this plugin's
folder without naming it.

(plugin-hooks)=
## The plugin's C hooks

Every Rack plugin library exports `init(Plugin*)` and `destroy()`. RackPy's
host implements them. `init` imports the trusted entry module, with this
plugin as `plugin_instance()`, then adds the models it registered.
`destroy()` runs the callbacks registered with `on_destroy(callback)`
(`rackpy.plugin.on_destroy`), most recent first on the main thread, with
errors logged, then releases the models' Python factories before Python
stops. `settingsToJson` and `settingsFromJson` are RP-15D.

Everything here is main-thread only (`ThreadError`) except the pure string
functions `is_slug_valid`, `normalize_slug`, `asset.system` and `asset.user`,
and the same Rack object always gives the same Python object. `createModule` and
`createModuleWidget` are Rack's to call; calling them from Python, with a way
to put the result in the rack, comes with the app widgets (RP-28).
