# Register models

**Implemented (RP-16A, RP-16B), Linux x86_64 inside Rack Free 2.6.6.** Python module
widgets are not available yet (RP-28), so every model gets a plain panel, and
modules do not process audio yet (RP-17). Plugin settings are RP-15D.

A Rack plugin offers *models*: the entries in Rack's module browser. Each
model makes *modules*, the instances in a patch. In a RackPy plugin, the
trusted entry module (named by `entry` in `rackpy-loader.json`) adds its
models while Rack loads the plugin:

```{rackpy-example} register_models.py
```

`plugin.add_model(slug, module, hp=...)` takes the model's slug and how to
build its modules, either way shown above:

- a *module factory*, the same constructor callback you give `Module.create`
  ([module construction](../reference/engine-module.md)): configure the module
  there and set its [state handlers](state.md);
- a `ModuleClass` subclass: Rack's module gets one instance, which
  `configure()` sets up through `self.module`, and whose `to_json()` and
  `from_json()` are the module's state handlers. The instance lives exactly
  as long as Rack's module, so keep per-module Python state on it.

Rack builds a module every time it needs one, on its main thread: when you
add it from the browser, when a patch loads, when you undo a deletion or
duplicate a module. Do not start threads or touch files there.

## plugin.json lists the same models

Rack reads a plugin's models from two places and rejects the plugin unless
they match: what `init()` registers and the `"modules"` list in
`plugin.json`, which also gives each model its name, description and tags:

```json
"modules": [
  {"slug": "PyControls", "name": "Controls", "description": "A level control", "tags": ["Utility"]},
  {"slug": "PyNotes", "name": "Notes", "description": "Text saved with the patch", "tags": ["Utility"]}
]
```

RackPy checks this when the entry module returns. If a slug is listed but
not added, or added but not listed, RackPy adds **no** model, shuts its Python
down and writes the difference to Rack's log
(`RackPy host initialization failed (4): ... listed but not added: ...`).
Rack then skips the plugin and keeps running. The same happens if the entry
module raises (`failed (3)`): models it added before the error are dropped.

Models can be added only during that import. `add_model` raises
`OwnershipError` later, `ValueError` for a slug that is not valid
(`rackpy.raw.plugin.is_slug_valid`: letters, digits, `-` and `_`) or already
added, and `TypeError` for a factory that is not callable.

## When the entry module fails

Rack's log (`log.txt` in the Rack user folder) gets the traceback and a
line saying what failed, `RackPy host initialization failed (3): ...`. An
entry module that does not exist is named as such, and so is a module it
imports that the plugin does not ship: RackPy runs only the plugin's own
files (`pythonpath/`) and Python's standard library, never packages
installed elsewhere on the computer.

## When a module factory fails

Rack expects a module every time it asks for one and cannot handle a failure,
so RackPy never lets one reach it. If the factory raises, the error goes to
Rack's log and `rackpy.runtime.diagnostics()`, and Rack gets a
**placeholder**: a module without controls, on a panel labelled
"(failed)". It keeps everything the patch saved for that module and writes it
back unchanged, so saving the patch does not lose the module's state. Fix
the script and reload the patch to get the real module back.

## Files and shutdown

`rackpy.assets.plugin("res/table.json")` is a file in the plugin's own
folder, `rackpy.assets.system(...)` one in Rack's, both read-only, and
`rackpy.assets.user(...)` one in Rack's user folder, which you may write. They
return `pathlib.Path`. Files that belong to one module in one patch go in its
[patch storage](state.md#files-next-to-the-patch) instead.

When Rack exits it first closes its context, which runs
`rackpy.runtime.on_context_closing` callbacks while Rack's objects still
work, and later calls the plugin's `destroy()`, which runs
`rackpy.plugin.on_destroy` callbacks. By then every handle is stale, so use
`on_destroy` for Python-only cleanup.

## Threads and lifetime

Plugins and models belong to Rack for its whole run; the Python objects
(`rackpy.raw.plugin.Plugin` and `Model`) are handles to them, used on the main
thread only. A model added from Python keeps its factory until Rack shuts
down, and RackPy releases it before Python stops.
