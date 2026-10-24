# Plugins and models

`rackpy.raw.plugin` is `rack::plugin`. **Implemented (RP-16A), Linux x86_64
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
reason to change them. `models` is a tuple snapshot; `get_model(slug)` and
`get_brand()` are Rack's methods. `add_model(model)` is Rack's
`Plugin::addModel`, for this plugin only and only while it initializes; the
models are added to Rack when initialization ends, all or none
([plugin.json](../how-to/plugins.md#pluginjson-lists-the-same-models)).

(plugin-models)=
## Models

`create_model(slug, module_factory, *, hp=4)` is Rack's
`createModel<TModule, TModuleWidget>`: Rack calls `module_factory(module)`,
Module.create's constructor callback, whenever it creates the model's module,
with errors contained (a [placeholder](../how-to/plugins.md#when-a-module-factory-fails)
replaces the module). `hp` sizes the plain panel each module gets until
Python module widgets exist (RP-28). The new `Model` belongs to Python until
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

Everything here is main-thread only (`ThreadError`), and the same Rack
object always gives the same Python object. `createModule` and
`createModuleWidget` are Rack's to call; calling them from Python, with a way
to put the result in the rack, comes with the app widgets (RP-28).
