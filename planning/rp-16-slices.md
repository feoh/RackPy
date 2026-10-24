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

**Still on the parent:** Python module widgets (`createModuleWidget` with a
Python `ModuleWidget`, which belongs to RP-28), the Module class facade,
trusted-script locations and import diagnostics (missing scripts and
dependencies, version behaviour), asset lookup (`rack::asset`), the
`init`/`destroy` C ABI records, `Plugin::handle`, `plugins`/`pluginsPath`,
`Model::appendContextMenu` (menus, RP-26), and plugin settings (RP-15D).
