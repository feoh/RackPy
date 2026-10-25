# RP-16 bounded implementation slices

These child contracts supplement, not replace, [RP-16 in TASKS.md](TASKS.md).
Live Witan status is authoritative. RP-16 waits for its children; its original
acceptance criteria stay on the parent.

**Owner decisions 2026-10-24:** registration uses callback factories with a
class facade on top: raw `rackpy.raw.plugin.create_model(slug,
module_factory)` mirrors `createModel<TModule, TModuleWidget>` and reuses
`Module.create`'s constructor callback, `rackpy.plugin.plugin.add_model(...)`
is the facade, and a Module class facade installs its methods through the
same constructor-time mechanism. `plugin.json`'s `"modules"` is the source of
truth: RackPy checks registration against it when initialization ends and
adds all models or none; no code runs at build time.

**RP-16A — Python model registration checked against plugin.json**
(`tk-rackpy-16a-python-model-registration-checked-aga-dc68a7`). Native
`rackpy::plugin::Model` (`native/plugin/`) runs a Python module factory
through `Module.create`'s transaction whenever Rack calls `createModule`, and
hands Rack the sealed module. Rack asserts that it gets a module and never
catches, so a failing factory is contained and Rack gets a
`PlaceholderModule`, which keeps the module's patch JSON and writes it back
unchanged. `createModuleWidget` builds an original plain panel (`hp` wide)
with the model's name, also for the browser's null-module preview. Models
are staged while the entry module is imported and committed at the end of
host init against `plugin.json`; on a mismatch, or when the entry module
raises, none is added, the host shuts Python down with an actionable log
line, and Rack skips the plugin. Factories are released before
finalization; Rack deletes the models after `destroy()`. 44 records are
implemented: `Plugin` and `Model` with their fields and Python-relevant
methods, `Plugin::addModel`, `createModel`, the lookups and the slug rules.
Tests run in actual Rack: the browser's add path, deletion and undo, the
exit autosave and the reload at the next launch, the preview, a factory that
fails on reload, and refused registrations. Evidence:
[RP-16A validation](rp-16a-validation.md).

**RP-16B — assets, plugin hooks, entry diagnostics and the Module class facade**
(`tk-rackpy-16b-assets-plugin-hooks-entry-diagnostics-5f4827`).
`rackpy.raw.asset` (`system`, `user`, `plugin` and Rack's four directory
variables) with the `rackpy.assets` pathlib facade; the `init`/`destroy` C
hooks (`init` imports the entry module and commits its models, `destroy`
runs `on_destroy` callbacks newest first before factories are released);
`plugins` as a live read-only `PluginList`, `pluginsPath`, `Plugin.handle`
as an opaque `LibraryHandle`, `Model.from_json` for this plugin's models
(type-checked first: Rack dereferences the strings it reads), and the two
destructors' observable semantics. A missing entry module or a missing
dependency is named in Rack's log with the traceback. `rackpy.plugin.ModuleClass`
is the owner-approved class facade: one instance per module, `configure()`,
`to_json()` and `from_json()`, installed through the same constructor
callback and `set_data_handlers`. 15 more records are implemented.

## RP-16 closure (2026-10-24)

Owner decisions 2026-10-24: the remaining records move to the tasks that own
their types. Python-callable `Model::createModule` and
`Model::createModuleWidget`, and Python module widgets for `createModel`'s
`TModuleWidget` (so `createModel` stays planned), go to RP-28;
`Model::appendContextMenu` goes to RP-26. `Plugin::fromJson` is bound for
this plugin while it initializes, with the fields restored if Rack raises;
`Plugin::modulesFromJson` is bound the same way as a check against the staged
models, because Rack runs the real one after initialization. Plugin settings
are RP-15D, which RP-16's closure unblocks.

**RP-16C — Python module lifecycle overrides and Module.model**
(`tk-rackpy-16c-python-module-lifecycle-overrides-and-aaeb34`). This is a
follow-up after closure: RP-14C3's notes still assigned these records to
RP-16 when it closed.

- `Module.set_event_handlers(...)`, constructor-only, and the overridden
  `ModuleClass` methods handle `on_add`, `on_remove`, `on_bypass`,
  `on_un_bypass`, `on_port_change`, `on_reset`, `on_randomize` and
  `on_save`.
- RackPy's native adapters call them after Rack's behaviour, through a
  Python-free `EventHandlers` interface, on the main thread with errors
  contained.
- Handlers are skipped once the context is closing. A ui-exclusive handler
  over 1 ms records `slow_exclusive_callback`.
- `Module.model` and `get_model()` return plugin `Model` handles.
- 14 records are implemented, including Rack's deprecated no-argument
  spellings, which the same handler covers.
- Sample-rate and master events stay with RP-22.
