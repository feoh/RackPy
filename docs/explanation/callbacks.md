# Callbacks and the laboratory

Rack calls back into a module from several threads. Where a callback runs
decides whether Python may run there, and RackPy's rules follow from that
([ADR 003](../decisions/003-lifetimes-and-threads.md), the
[callback matrix](../reference/callback-matrix.md)).

:::{admonition} The laboratory is not real-time audio
:class: warning

The callback laboratory runs Python versions of a module's audio callbacks
**offline**, for experiments, teaching and tests. Rack's engine never runs a
laboratory module, a patch can never create or enable one, and nothing about
its speed says anything about production performance. Production modules
process audio with [native kernels](../how-to/dsp.md).
:::

## Why Python never runs on Rack's audio threads

Rack calls a module's `process()` once per sample, on its engine threads,
while it holds its engine lock. Python there would need the GIL, which any
other Python thread may hold for milliseconds; at 48 kHz a frame lasts about
21 µs. RP-07 measured it: with one busy Python thread, the 99th percentile of
a per-frame Python call was about 1.1 ms ([ADR 004](../decisions/004-performance-budgets.md)).
So in normal use RackPy refuses every Python override of a callback Rack can
run on an engine or audio driver thread:

- a {py:class}`rackpy.plugin.ModuleClass` that defines `process`,
  `process_bypass`, `step`, `on_sample_rate_change`, `on_set_master`,
  `on_unset_master` or `on_expander_change` is refused with `TypeError` when
  the class is defined, and the message points to native kernels and to the
  laboratory;
- `Module.set_event_handlers` refuses the sample-rate and master events;
- a Python `Quantity` cannot define the value methods Rack also calls from
  its engine thread ([values](../reference/values.md#quantity)).

## Why the laboratory is a bench, not Rack's engine

The obvious design, running a Python `process()` inside Rack's own engine on
a laboratory switch, cannot be built from Rack's public API (RP-22, step 1).
Rack's engine threads call `Module::doProcess`, which picks `process` or
`processBypass`; it, and everything a host needs to imitate the engine, is
`PRIVATE` in the pinned SDK:

| Declaration (`include/`) | Why a host needs it |
| --- | --- |
| `engine::Module::doProcess(const ProcessArgs&)` | the per-frame call that chooses process or processBypass |
| `engine::Module::setBypassed(bool)` | the bypass state processBypass depends on |
| `engine::Module::setExpanderModule(Module*, uint8_t)` | expander pointers between neighbours |
| `engine::Engine::Engine()`, `~Engine()` | a second, offline engine |

Inside Rack's real engine, a Python `process()` would run on engine worker
threads (where Python is refused) or block them on the GIL. So the
laboratory provides its own host instead.

## The laboratory bench

A {py:class}`rackpy.laboratory.Bench` is an offline host for RackPy modules.
Python steps it on Rack's main thread, and it does what Rack 2.6.6's engine
does, using only public API:

- `step(frames)` processes every module once per frame, in the order they
  were added, calling `process`, or `process_bypass` for a bypassed module,
  with Rack's `ProcessArgs` (sample rate, sample time, frame); then it moves
  every cable's voltages (one cable copies its output's channels, stacked
  cables sum, non-finite voltages become 0), then flips requested expander
  messages. Signals through a cable therefore arrive one frame later, and
  feedback loops behave as in Rack.
- `add`, `remove`, `connect`, `disconnect`, `set_bypassed`,
  `set_sample_rate`, `set_master_module`, `reset_module`,
  `randomize_module`, `prepare_save` and `prepare_save_module` dispatch the
  events Rack's engine dispatches, in the same order: adding gives
  `AddEvent` then `SampleRateChangeEvent`; a cable gives a `PortChangeEvent`
  to each port it newly connects; bypassing clears the outputs first;
  removing gives `RemoveEvent`, then `UnsetMasterEvent` if it was the
  master, then clears its neighbours' expanders and its own.
- `set_expander(module, side, neighbour)` places a module beside another,
  as Rack's rack does for adjacent module widgets (it sets the expander's
  module ID); at the next `step`, as in Rack's `stepBlock`, the expander's
  pointer follows and each module whose neighbour changed gets an
  `ExpanderChangeEvent`. Rack's own `setExpanderModule` is `PRIVATE`; the
  bench repeats what it does with the public `Expander` fields.

It runs ordinary RackPy modules (with native kernels) and laboratory modules
side by side. In the live-Rack tests, a patch of kernels, a bypass route,
stacked and polyphonic cables, non-finite voltages and a feedback loop gives
the same port values on a bench as in Rack's engine, frame for frame
(`tests/laboratory/in_rack/test_engine_reference.py`). The same sequence of
edits made in Rack's engine and on a bench gives each module the same
events, in the same order (`tests/laboratory/in_rack/test_engine_events.py`).

What a bench does **not** reproduce: engine worker threads and their
scheduling, the audio driver thread and master-module timing, parameter
smoothing, the CPU meter, plug lights, and Rack's random module IDs (a bench
gives the lowest free one). A module's own `is_bypassed()` reads Rack's
engine and stays `False` on a bench, and Rack's `toJson` does not save a
bench's bypass state; ask the bench. A bench has no audio clock, so its
master module is only a designation.

## Laboratory modules

Trusted code enables the laboratory with {py:func}`rackpy.laboratory.enable`;
until then every laboratory call raises
{py:class}`~rackpy.errors.LaboratoryError`. A laboratory module is a
{py:class}`rackpy.laboratory.ModuleClass`, which is a
{py:class}`rackpy.plugin.ModuleClass` that may also override `process`,
`process_bypass` and the deprecated `step`, every module event and Rack's
JSON virtuals:

```{rackpy-example} laboratory_bench.py
```

`super().process(args)` and `super().process_bypass(args)` run Rack's own
implementations, as in a C++ subclass: Rack's `process` calls `step()`, and
its `processBypass` copies each bypass route's input to its output.

**Events and JSON.** The event methods (`on_add`, `on_remove`,
`on_bypass`, `on_un_bypass`, `on_port_change`, `on_reset`, `on_randomize`,
`on_save`, `on_sample_rate_change`, `on_set_master`, `on_unset_master`,
`on_expander_change`) and the JSON methods (`module_to_json`,
`module_from_json`, `params_to_json`, `params_from_json`, `data_to_json`,
`data_from_json`) are called synchronously, where Rack would call them, and
*replace* Rack's implementation: `super()` runs it. That is the difference
from {py:class}`rackpy.plugin.ModuleClass`, whose `on_reset` runs *after*
Rack has reset the parameters; a laboratory `on_reset` that does not call
`super()` resets nothing, as in C++. Rack's own `toJson` calls the Python
`params_to_json` and `data_to_json` and uses what they return; returning
`None` gives Rack's `NULL`. `module_to_json`/`module_from_json` are Rack's
`Module::toJson`/`fromJson` (`ModuleClass.to_json` remains the custom-data
handler that `data_to_json`'s `super()` calls). Rack's `toJson` and
`fromJson` name the module's model, so `super()` there needs
`ModuleClass.create(model)`; the model only names the module. Parameter
values go through Rack's engine, so parameter JSON, reset and randomize of a
module with parameters need Rack's live context (`ContextError` otherwise).

The rules that keep it contained:

- **Never in Rack's engine.** `Engine.add_module` refuses a laboratory
  module, `ModuleClass.factory()` refuses a laboratory class (so no model
  can create one, and no patch), and `set_kernel` refuses it (its audio is
  its Python `process`).
- **Errors go to the caller.** An override that raises stops the frame
  before the cables step, and `Bench.step` raises the error; an error inside
  Rack's code called through `super()` reaches the override that called it.
  An event override that raises does not stop the operation (Rack's engine
  would carry on): the other modules still get their events, and the bench
  call raises the first error afterwards.
- **No reentry.** A callback may not step, edit or clear any bench, as a C++
  callback may not re-enter Rack's non-recursive engine lock; that raises
  `LaboratoryError`. Callbacks nested through Rack's code (a `process` that
  calls `super().process`, whose `step` calls `process`, ...) stop with
  `RecursionError` after 200 levels.
- **Main thread only**, like the rest of the module API.
