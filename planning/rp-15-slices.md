# RP-15 bounded implementation slices

These child contracts supplement, not replace, [RP-15 in TASKS.md](TASKS.md).
Live Witan status is authoritative. RP-15 waits for its children; its original
acceptance criteria stay on the parent.

**Owner decision 2026-10-23:** a module's custom state uses handlers set in
the constructor. Inside `Module.create`'s callback,
`set_data_handlers(to_json=..., from_json=...)` registers callables the native
module calls synchronously when Rack saves or loads; errors are contained when
Rack calls. RP-16's Python module classes later install their methods through
the same mechanism.

**RP-15A — module and quantity JSON with constructor-set data handlers**
(`tk-rackpy-15a-module-and-quantity-json-with-constru-e7ac5e`). Exact scope:
`Module::toJson`, `fromJson`, `paramsToJson`, `paramsFromJson`, `dataToJson`,
`dataFromJson` and `ParamQuantity::toJson`/`fromJson`. The handlers live
behind a Python-free native interface; Python callers see handler and
override errors, Rack callers get them contained (no data saved, the rest of
the load intact). Missing data never calls `from_json`; JSON null passes as
`None`; `None` from `to_json` saves nothing. `ParamQuantity` subclasses may
override `to_json`/`from_json`. Whole-module JSON needs a model (RP-16);
engine-owned modules go through the engine's locks. The runtime JSON policy
applies. The module handle reports handlers and transferred slot objects to
Python's garbage collector. Patch storage, plugin settings and the `.vcv`,
autosave and undo integration stay on later RP-15 children.

**RP-15B — patch storage directories**
(`tk-rackpy-15b-patch-storage-directories-268c1b`). Exact scope:
`Module::getPatchStorageDirectory()` and `createPatchStorageDirectory()`.
Raw methods return Rack's path strings; `Module.patch_storage(create=True)`
returns a `pathlib.Path`. An engine ID is required (Rack's own exception
otherwise), as are the main thread and Rack's live context; nothing deletes
storage, which Rack garbage-collects on save so that undo can restore it.
