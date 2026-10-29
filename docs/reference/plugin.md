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
`Plugin::addModel`, for this plugin only while it initializes; the
models are added to Rack when initialization ends, all or none
([plugin.json](../how-to/plugins.md#pluginjson-lists-the-same-models)).

`Plugin()` creates a separate, **isolated** Python-owned Rack plugin on the
main thread. It is not registered with Rack and cannot be used to load a plugin
or create modules. It can attach detached models with `add_model`; they remain
owned and cannot be attached elsewhere. Call `close()` on the main thread when
done. It invokes Rack's plugin destructor once, clears attached models' plugin
back-pointers, and leaves any model held by Python alive. `close()` is
idempotent; the plugin handle is then stale. Rack-owned plugins cannot be
closed from Python. Dropping the Python plugin handle does not close it:
RackPy retains isolated plugins until explicit close or Rack context shutdown,
so garbage collection on another thread cannot destroy them. Failing to close
many isolated plugins retains their models and factories until shutdown.

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

`create_model(slug, module_factory, *, widget_factory=None, hp=4)` is Rack's
`createModel<TModule, TModuleWidget>`:

- Rack calls `module_factory(module)`, Module.create's constructor callback,
  whenever it creates the model's module. Errors are contained: a
  [placeholder](../how-to/plugins.md#when-a-module-factory-fails) replaces
  the module.
- Rack calls `widget_factory(module)` for the module's widget, with `None`
  for a browser preview ([module widgets](app.md#module-widgets)).
- Without a widget factory, the module gets RackPy's plain panel, `hp` wide.

The new `Model` belongs to Python until it is committed to Rack. An isolated
plugin retains attached models, which remain Python-owned.
`Model.create_module()` and `Model.create_module_widget(module=None)` make a
module or widget of a committed model, a RackPy model's or another plugin's,
which Python then owns ([modules from other plugins](app.md#modules-from-other-plugins)).
Detached models, models attached to isolated plugins, and models staged during
plugin initialization cannot create modules or widgets (`OwnershipError`): those objects retain a raw model pointer,
while a detached model can be deleted with its Python handle and a staged model
can be deleted if registration fails. Wait until registration has committed.

A `Model` has Rack's fields. `slug` can change only before the model is
added: Rack and patches look models up by it. `plugin` is its plugin, or
`None` before it is added or after its isolated plugin is closed. `name`, `description`, `manual_url`,
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
stops. `settingsToJson` and `settingsFromJson` call the handlers set with
`set_settings_handlers(to_json=None, from_json=None)`
(`rackpy.plugin.Plugin.set_settings_handlers`); see
[plugin-wide settings](../how-to/state.md#plugin-wide-settings). The loader
forwards all four hooks to the host library (host ABI 2). Rack's separate
`Plugin` destructor clears attached models' plugin pointers without deleting
the models. RackPy currently exposes borrowed plugin views; a Python mapping
and behavioral proof of that destructor remain open (RP-31D). The `destroy()`
hook is not the destructor.

Everything here is main-thread only (`ThreadError`) except the pure string
functions `is_slug_valid`, `normalize_slug`, `asset.system` and `asset.user`,
and the same Rack object always gives the same Python object. `createModule` and
`createModuleWidget` are Rack's to call; calling them from Python, with a way
to put the result in the rack, comes with the app widgets (RP-28).
