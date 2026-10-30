# The engine

`rackpy.raw.engine.Engine` is Rack's `rack::engine::Engine`, reached as
`Context.engine`. **Implemented (RP-24A, RP-24B), Linux x86_64 inside Rack
Free 2.6.6:** every public `Engine` method (modules, the master module,
timing, parameters, cables, parameter handles and the engine's JSON), with
`Cable` and `ParamHandle` (see the [coverage report](coverage.md)). The
history is RP-24C.

```{rackpy-example} engine.py
```

(engine-threads)=
## Threads and Rack's engine lock

Every method runs on Rack's main thread, in the live bound context. A method
that takes Rack's engine lock (Rack's `Engine.hpp` says which) releases the
GIL first, so Rack's engine thread and Python callbacks inside the call
(a module's `on_reset`, its JSON handlers) keep running. Called from a
callback that runs while Rack already holds the lock exclusively (a
`ResetEvent`, say), such a method raises `EngineLockError` instead of
deadlocking.

Methods that Rack does not lock (`get_frame`, `get_sample_rate`,
`get_param_value` and the like) read Rack's values directly.

Rack's `_NoLock` variants (`get_module_no_lock`, `set_master_module_no_lock`,
`get_param_handle_no_lock`, `update_param_handle_no_lock`) need the caller
to hold the lock. Python holds it only inside a callback Rack runs under the
exclusive lock, which RackPy tracks: there they are the methods to use, and
anywhere else they raise `EngineLockError`.

(engine-modules)=
## Modules

`get_module_ids()` lists the engine's module IDs, and `get_module(id)` returns
the module with that ID: a RackPy module's own `Module` handle, an
`EngineModule` for any other module, or `None`. An `EngineModule` holds only
the ID and finds the module again at every use, so it goes stale
(`StaleHandleError`, `alive` false) once the module leaves the engine; it
never holds Rack's pointer. It reports the model, bypass state and component
counts. Two handles for one ID are equal. `Model.create_module()` for
another plugin's model also returns an `EngineModule`, which Python owns
(`in_engine` false) until `add_module`
([modules from other plugins](app.md#modules-from-other-plugins)).

`get_num_modules()` returns the count; `has_module(module)` tests whether a
module is in the engine.

`reset_module`, `randomize_module` and `bypass_module` are Rack's: they send
the module its event under the lock, without an undo step.
`module_to_json`, `module_from_json`, `prepare_save_module` and
`prepare_save` are Rack's module JSON and save events. All of them take a
module in the engine, a `Module` or an `EngineModule`.

### Ownership

Rack's engine holds its modules, but each module widget owns its module, and
Rack deletes a module with its widget. RackPy follows that:

- `add_module(module)` gives Rack's engine a module Python owns. It must have
  a model of this plugin (`Model.create_module` makes one), since Rack saves a
  module with its plugin and model; a constructor callback cannot add its own
  module. Its handles keep working, and `get_module` returns them.
- `add_module(module)` also takes another plugin's module that Python owns
  (from `Model.create_module`).
- `remove_module(module)` takes a RackPy module out of the engine and gives it
  back to Python. Rack refuses nothing here, so RackPy refuses a module that a
  widget owns (delete the widget with its `remove_action`), that controls
  still point at, or that cables still connect. Another plugin's module comes
  back to Python only when no module widget owns it and no cables touch it.
- `clear()` and `from_json(data)` delete every module and cable in the
  engine, so they are refused while Rack's rack has module or cable widgets.
  Rack's patch manager clears the rack first.

(engine-master)=
## The master module and timing

The master module is the one whose audio clock steps the engine;
`get_master_module()` returns its handle or `None`, and
`set_master_module(None)` gives that back to Rack's own thread. Rack's
`stepBlock` is for the master module alone, so `step_block(frames)` needs a
RackPy module as the master: it then advances the engine by that many frames
(1 to 65536), and Rack's own thread waits. Rack serializes it with any other
stepping.

`get_sample_rate`, `get_sample_time`, `get_frame`, `get_block`,
`get_block_frame`, `get_block_time`, `get_block_frames`,
`get_block_duration`, `get_meter_average` and `get_meter_max` are Rack's
timing values. `set_suggested_sample_rate(rate)` sets the rate unless Rack's
settings fix one; `0` gives Rack's fallback. `yield_workers()` is Rack's hint
that its worker threads should block rather than spin.

(engine-params)=
## Parameters

`set_param_value(module, param_id, value)` and `get_param_value` set and read
a parameter's raw value at once. `set_param_smooth_value` moves it smoothly
toward the value as the engine steps, and `get_param_smooth_value` returns the
target, or the value when nothing is smoothing. Rack indexes parameters
unchecked, so RackPy checks every ID. Rack keeps the smoothed module's raw
pointer, so smoothing needs a module in the engine.

(engine-cables)=
## Cables

A `Cable()` that Python makes holds Rack's struct: `id` (-1 until the engine
assigns one), `output_module` and `output_id`, `input_module` and
`input_id`. Its modules are module handles (`Module` or `EngineModule`),
never Rack's pointers. `to_json()` is Rack's patch JSON for it, and
`from_json(data)` sets it from patch JSON, finding the modules in Rack's
engine by ID (Rack's `Exception` becomes `ValueError`).

`get_cable(id)` returns the engine's cable with that ID as a `Cable` that
finds it again by ID at every use: Rack deletes cables without telling
anyone (a cable widget its own, `Engine.clear` the rest), and the handle
then goes stale (`alive` false). Such a cable's fields are read-only.
`get_cable_ids()`, `get_num_cables()` and `has_cable(cable)` are Rack's.

`add_cable(cable)` gives Rack's engine a cable Python made, and the handle
then finds it by ID. Rack asserts, as it deletes a module, that no cable in
the engine still touches it, and a module widget deletes its module. A cable
with no cable widget to remove it first may therefore join only RackPy
modules that Python put into the engine itself (`add_module`) and that no
module widget owns. Giving such a module a widget is refused while the cable
is there. Port IDs are checked. Cables with widgets, which users see and
drag, are `RackWidget`'s (RP-28E).

`remove_cable(cable)` takes a cable that no cable widget owns out of the
engine, and Python owns it again (with its ID). Other handles to it go
stale.

(engine-param-handles)=
## Parameter handles

A `ParamHandle` is Rack's weak link to one module's param, as Core's MIDI-Map
uses: Rack's engine keeps the module pointer up to date, and the param's
widget shows the mapping's `color` and `text`.

- `ParamHandle()` makes a blank handle (`module_id` -1). `add_param_handle`
  gives it to Rack's engine, which only takes blank handles.
  `update_param_handle(handle, module_id, param_id, overwrite=True)` maps it,
  and `module_id` -1 unmaps it. With `overwrite`, a handle already on that
  param is unmapped; without, this one is. Rack 2.6.6 also unmaps a handle
  that is mapped again to the param it is on, because it finds the handle
  itself there. RackPy checks the param ID when the module exists. While the
  engine has the handle, its IDs change only this way.
- `get_param_handle(module_id, param_id)` returns the handle on a param:
  Python's own, a read-only view of another owner's (a module's), found
  again by its IDs, or `None`. Passing the module itself is Rack's deprecated
  form.
- `module` is the module once it is in the engine, else `None`.
- `remove_param_handle` takes Python's handle out of the engine.

Rack's engine keeps raw pointers to its handles, so RackPy keeps each handle
Python added allocated while the engine has it, even if Python drops the
object. Rack's `Engine.clear` (a new or loaded patch) removes every handle
without telling its owner, and Rack has no call that says whether it did.
RackPy detects the clear with a private sentinel handle of its own, mapped
to a module ID Rack never assigns (2\*\*53) and never returned. After a
clear, `in_engine` is false and `remove_param_handle` is refused, where
Rack's own call would fail its assertion. Handles still in the engine when
Rack's context closes are removed while the engine lives.

(engine-json)=
## The engine's JSON

`to_json()` is the engine's part of a patch: modules, cables and the master
module. `from_json(data)` clears the engine and creates the patch's modules
and cables, and is refused while Rack's rack has widgets.

(engine-host)=
## The host facade

`rackpy.engine.host` puts the same calls in a more Pythonic shape, for
scripts working on Rack's running patch:

- `engine()` and `history()` return `Context.engine` and `Context.history`,
  and raise `ContextError` outside Rack's live UI.
- `modules()`, `module(module_id)` and `cables()` list the engine's modules
  and cables.
- `connect((module, output_id), (module, input_id))` returns the new
  `Cable`, and `disconnect(cable)` removes it, under the rules in
  [Cables](#cables).
- `map_param(module, param_id, text=..., color=...)` returns a mapped
  `ParamHandle`, and `unmap_param(handle)` removes it.
- `undo_step(name)` is a context manager that yields a `ComplexAction`:
  make each change, push its action to it, and the step goes to Rack's
  history when the block ends.

Every call is synchronous. It runs Rack's code on the main thread before
returning, and nothing is queued. A failed call leaves the engine as it was:
`map_param` removes its handle again, and when an `undo_step` block raises,
the actions it collected are undone and nothing is pushed.
