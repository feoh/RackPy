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

**RP-15C — JSON boundary and patch storage hardening**
(`tk-rackpy-15c-json-boundary-and-patch-storage-harde-482fdd`). Exact scope:
the `json_t` boundary (`native/runtime/json.cpp`) as Rack and Python reach it
through module and quantity JSON, and the patch storage facade's failures.
Test hooks write and read a module's JSON as text exactly as Rack writes and
reads a patch (`json_dumpf` with `JSON_INDENT(2)`, `json_loadf` with no
flags), count the jansson blocks the main thread leaves allocated (a counting
allocator chained through `json_set_alloc_funcs`, with a positive control and
a mutation check) and report reference counts. Findings fixed here: strings
containing NUL are refused like NUL keys, because Rack writes `\u0000` but
refuses it on load, so one such string made the whole patch unopenable
(reverses RP-12's NUL-string round trip); a `ParamQuantity.to_json` override
must return a `dict`, because `paramsToJson` adds `"id"` to it; and
`Module.patch_storage()` raises `OSError` when Rack's
`createPatchStorageDirectory` silently fails (the raw method keeps Rack's
behaviour). Patch data RackPy cannot convert (deeper than 64 levels, not
UTF-8) is contained and diagnosed with the rest of the load intact. The how-to
example saves a schema version and handles old, newer and malformed data.

Deliverable paths: the boundary is `native/runtime/json.cpp`, the bindings
live on `rackpy.engine.Module` and `ParamQuantity`, and the JSON type is
`rackpy.types.JSON`; there is no separate `native/state/`, `rackpy.state` or
`tests/state/`, whose content these already hold.

**Still on the parent, needing RP-16:** plugin-wide settings callbacks
(`settingsToJson`/`settingsFromJson`) and a real `.vcv` save, reopen,
autosave and undo round trip of Python modules, which need registered models
whose factories run the Python constructor. Split to RP-15D, which RP-16
does not wait for, so there is no dependency cycle.
