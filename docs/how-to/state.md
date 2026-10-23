# Save and restore a module's state

**Implemented (RP-15A), Linux x86_64 inside Rack Free 2.6.6.** Patch storage
directories, plugin-wide settings and a full `.vcv` save, reopen, autosave and
undo round trip are later RP-15 work; modules cannot be registered as models
yet (RP-16).

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
  invalid Unicode (`UnicodeError`) and nesting deeper than 64 levels
  (`ValueError`).

## Errors

When your own code calls `module.data_to_json()` or `module.data_from_json()`,
a handler's exception reaches you. When Rack calls a handler during a save or
a load, RackPy catches the exception, records it in
`rackpy.runtime.diagnostics()`, and lets Rack carry on: a failed `to_json`
saves no data, and a failed `from_json` leaves the rest of the load (the
parameters, bypass) intact. A failed `ParamQuantity.to_json` override falls
back to Rack's own JSON for that parameter.

## Untrusted patches

A patch file is data from whoever made it. Validate what `from_json` receives
and ignore what you do not understand, as `load()` above does: unknown fields,
wrong types, values from a future version of your module. RackPy never
executes code from a patch and never unpickles anything; do not add a handler
that does.

## Calling Rack's JSON yourself

`module.params_to_json()` and `module.params_from_json(data)` run Rack's
parameter serialization; they need Rack's live bound UI context, because the
values go through Rack's engine. `module.to_json()` and `module.from_json()`
run Rack's whole-module serialization, which names the module's plugin and
model, so they need a registered model (RP-16). For a module Rack's engine
owns, these go through the engine's locks exactly as a save or load does, and
Python inside them is then contained as in Rack. All of these need a sealed
module and the main thread.
