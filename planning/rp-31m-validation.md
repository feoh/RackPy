# RP-31M investigation — incomplete

Rack 2.6.6 `src/plugin/Plugin.cpp:14-20` shows `Plugin::~Plugin()` clears
`model->plugin` for each model and does not delete the models. RackPy exposes
loaded plugins as borrowed `PluginHandle` objects only; `Model.plugin` is
observable, but a Python caller cannot destroy the plugin. The existing
`on_destroy` test covers a different C hook, not this destructor.

One possible mapping is an isolated, Python-owned plugin with explicit close,
which calls the real destructor while retaining its models until their
back-pointers have been cleared. Model handles need identity, lifetime and
thread checks, including when models outlive the plugin or are dropped before
it. This must not register the isolated plugin in Rack or change loaded
plugins. It is not yet implemented or tested.

The user subsequently authorized proceeding with recommended decisions, so
the isolated-plugin exception no longer awaits an interactive choice. A
separate existing hazard remains: a module or widget made from a detached or
staged RackPy model stores a raw model pointer even though the model can be
freed when its Python handle is dropped or registration fails. As a WIP safety
restriction, `create_module` and `create_module_widget` now require a committed
model; tests cover detached and staged refusals. This also prevents those
escapes while an eventual isolated plugin owns detached models. It is a
restriction, not yet a complete Plugin destructor mapping.

`native/bindings/plugin.cpp` compiled with Werror, generated files and `prek`
(including mypy) passed. The fresh hooks build timed out after 36/235 steps;
the new host tests have **not** been run. No Plugin construction/destruction
implementation or host proof exists yet, and the destructor record remains
planned. The build may be resumed in `build/dev`; macOS and Windows are untested.
