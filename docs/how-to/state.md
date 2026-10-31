# Save and restore a module's state

**Implemented (RP-15A to RP-15D), Linux x86_64 inside Rack Free 2.6.6.**
Tested through Rack's own File > Save to a `.vcv`, its reopen, the autosave
and undo of a deleted module, with modules of [registered models](plugins.md).
The [state and events example](https://github.com/feoh/RackPy/tree/main/examples/state-and-events)
is a whole plugin that keeps state in all three places below.

Rack saves a module as JSON: its parameters, whether it is bypassed, and
whatever *custom data* the module adds. RackPy keeps Rack's split:

- **Parameters** are saved for you. Rack's `paramsToJson` asks each parameter
  quantity for its JSON; a `ParamQuantity` subclass may override `to_json` and
  `from_json`.
- **Custom data** comes from the handlers you set in the constructor callback
  with `module.set_data_handlers(to_json=..., from_json=...)`.

```{rackpy-example} module_state.py
```

## When your handlers run

Rack calls `to_json` synchronously while it saves (including autosave) and
`from_json` while it loads, both on its main thread. Rack's engine keeps
processing audio during a save, so read only state your Python code owns, as
`save()` above does; do not rely on values the engine is changing. During a
load audio is stopped, so keep `from_json` short: it holds up the engine, and
it may not call anything that takes Rack's engine lock (`EngineLockError`).

- `to_json()` returns JSON-compatible data: `None`, `bool`, `int`, `float`,
  `str`, `list`, `tuple` or `dict` with `str` keys. Returning `None` saves no
  data at all.
- `from_json(data)` receives what was saved. If the patch has no `"data"`,
  Rack does not call it; if the patch has JSON `null`, it receives `None`.
- RackPy refuses what JSON cannot hold: NaN and infinity (`ValueError`),
  integers beyond 64 bits (`OverflowError`), non-`str` keys (`TypeError`),
  invalid Unicode (`UnicodeError`), nesting deeper than 64 levels
  (`ValueError`) and NUL characters in strings or keys (`ValueError`): Rack
  writes NUL as `\u0000` but refuses it when it loads, which would make the
  whole patch impossible to open.
- A `ParamQuantity.to_json` override returns a `dict`: Rack adds the
  parameter's `"id"` to it (`TypeError` otherwise).

## Errors

When your own code calls `module.data_to_json()` or `module.data_from_json()`,
a handler's exception reaches you. When Rack calls a handler during a save or
a load, RackPy catches the exception, records it in
`rackpy.runtime.diagnostics()`, and lets Rack carry on: a failed `to_json`
saves no data, and a failed `from_json` leaves the rest of the load (the
parameters, bypass) intact. A failed `ParamQuantity.to_json` override falls
back to Rack's own JSON for that parameter.

## Untrusted patches and schema versions

A patch file is data from whoever made it. Validate what `from_json` receives
and ignore what you do not understand, as `load()` above does: unknown fields,
wrong types, values from a future version of your module. Save a schema
version with your data, as `save()` does, so that a later release can migrate
old patches and recognize newer ones instead of guessing. RackPy never
executes code from a patch and never unpickles anything; do not add a handler
that does.

A patch can also hold JSON that Python's values never produce, such as data
nested deeper than RackPy's 64 levels (jansson reads up to 2048) or, from a
buggy C++ plugin, strings that are not UTF-8. RackPy cannot convert those, so
`from_json` is not called; as with a handler's own error, the failure is
recorded in `rackpy.runtime.diagnostics()` and the rest of the load goes on.

## Calling Rack's JSON yourself

`module.params_to_json()` and `module.params_from_json(data)` run Rack's
parameter serialization; they need Rack's live bound UI context, because the
values go through Rack's engine. `module.to_json()` and `module.from_json()`
run Rack's whole-module serialization, which names the module's plugin and
model, so they need a registered model (RP-16). For a module Rack's engine
owns, these go through the engine's locks exactly as a save or load does, and
Python inside them is then contained as in Rack. All of these need a sealed
module and the main thread.

## Files next to the patch

Some state does not fit in JSON: samples, wavetables, large buffers. Rack gives
each module a *patch storage directory* inside the patch's autosave folder.
`module.patch_storage()` returns it as a `pathlib.Path`, creating it first
(`create=False` only computes it); `get_patch_storage_directory()` and
`create_patch_storage_directory()` are Rack's methods, returning strings.

The directory is named after the module's engine ID, so it exists only once
Rack's engine has added the module: before that, Rack raises its own
exception (`rackpy.raw.Exception`), which also means never inside the
constructor callback. Rack's advice applies: load files when the module is
added and write them when the patch is saved, never from audio processing.
These calls do filesystem work, so they need the main thread and Rack's live
context. Rack's `createPatchStorageDirectory` ignores a failure to create the
directory and returns the path anyway, and so does the raw method;
`patch_storage()` raises `OSError` instead. Files you look for may be absent
(a new module, a patch from elsewhere), so check before reading, as
`load_table()` does.

```{rackpy-example} patch_storage.py
```

RackPy never deletes this directory. Rack removes storage of deleted modules
when the user saves the patch; until then *undo* can bring a deleted module
back with its files, so do not delete them yourself when a module is removed.
When the patch is saved to a `.vcv`, Rack packs the storage into it, and
opening the `.vcv` unpacks it again.

## Plugin-wide settings

Some state belongs to the plugin, not to one module in one patch: a default
theme, the last folder used. Rack keeps it in its `settings.json`, under the
plugin's slug. Set the handlers from the entry module:

```{rackpy-example} plugin_settings.py
```

Rack calls `from_json(data)` once, right after the plugin initializes, and
only if `settings.json` has an entry for the plugin; that is why the
handlers belong in the entry module. It calls `to_json()` whenever it saves
its settings: periodically, and as it exits, when every handle to a Rack
object is already stale. Both run on Rack's main thread. If `to_json`
returns `None` or raises, or returns something JSON cannot hold, Rack keeps
the settings it stored before; errors are logged and recorded in
`rackpy.runtime.diagnostics()`. Users can edit `settings.json`, so validate
what `from_json` receives, as above.
