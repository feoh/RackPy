# The engine

`rackpy.raw.engine.Engine` is Rack's `rack::engine::Engine`, reached as
`Context.engine`. **Implemented (RP-24A), Linux x86_64 inside Rack Free
2.6.6:** modules, the master module, timing, parameters and the engine's
JSON. Cables and parameter handles are RP-24B, the history RP-24C (see the
[coverage report](coverage.md)).

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
`get_param_value` and the like) read Rack's values directly. Rack's
`_NoLock` variants need the caller to hold the lock, which Python never
does; they are RP-22's.

(engine-modules)=
## Modules

`get_module_ids()` lists the engine's module IDs, and `get_module(id)` returns
the module with that ID: a RackPy module's own `Module` handle, an
`EngineModule` for any other module, or `None`. An `EngineModule` holds only
the ID and finds the module again at every use, so it goes stale
(`StaleHandleError`, `alive` false) once the module leaves the engine; it
never holds Rack's pointer. It reports the model, bypass state and component
counts. Two handles for one ID are equal.

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
- `remove_module(module)` takes a RackPy module out of the engine and gives it
  back to Python. Rack refuses nothing here, so RackPy refuses a module that a
  widget owns (delete the widget with its `remove_action`), that controls
  still point at, or that cables still connect. Other plugins' modules leave
  the engine only with their widgets.
- `clear()` and `from_json(data)` delete every module and cable in the
  engine, so they are refused while Rack's rack has module or cable widgets.
  Rack's patch manager clears the rack first.

(engine-master)=
## The master module and timing

The master module is the one whose audio clock steps the engine;
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

(engine-json)=
## The engine's JSON

`to_json()` is the engine's part of a patch: modules, cables and the master
module. `from_json(data)` clears the engine and creates the patch's modules
and cables, and is refused while Rack's rack has widgets.
