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

## Every public virtual: domain, Python path or blocker

The [generated execution inventory](../reference/callback-paths.md) joins
**all 434 public virtual declarations** from `bin/callback-matrix` to
`bindings/manifest.json`: exact upstream ID (including overloads), execution
domain, Python names, status and per-symbol semantics. Regenerate it with
`uv run bin/generate_bindings.py generate`; `check` and
`tests/coverage/test_callback_paths.py` enforce freshness and the unchanged
denominator. It includes destructors (owner/GC lifetime mappings, not Python
virtual overrides) and `Exception::what` (a value/exception translation).

A callable wrapper or native adapter is **not** synchronous Python callback
coverage. The audit keeps these gaps as **full-release blockers**, with
status `planned`, retaining the existing native implementation and tests:

- **33 driver/device virtuals** have only native configured answers or queued
  notifications; these cannot replace synchronous return values or callback
  timing. RP-22F (`tk-rackpy-22f-laboratory-python-paths-for-audio-and-65ccf3`)
  owns their missing laboratory Python paths. The five `audio::Port` stream
  virtuals do have synchronous Python overrides on a **Python-driven device**,
  never on Rack's driver threads; queued Rack notifications are a different
  path ([audio](../reference/audio.md)).

The audit also kept **21 UI virtuals** on `PlugWidget`, `RackScrollWidget`,
`RackWidget` and `Scene` as blockers, because only borrowed host views
reached them. RP-28F
(`tk-rackpy-28f-python-subclass-trampolines-for-plugw-154b61`) found that no
plugin can override them: the last three have PRIVATE constructors, and only
`CableWidget`'s constructor makes a usable `PlugWidget`. By a decision
pending the owner's review ([coverage policy 11](../coverage.md#completeness-policy)),
the host views' calls on Rack's own instances are their mappings, and the
records are `implemented` again.

None of these entries disappear from the denominator, and this inventory
promotes nothing to `verified`. Laboratory paths prove only the documented
offline semantics, not driver/engine scheduling equivalence or support on
macOS/Windows.

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
  Because Rack's engine does not run a bench's modules, Python may use
  their [expanders](../reference/engine-module.md#expanders) directly:
  assign RackPy message buffers, write them, request a flip and read the
  swapped buffers after the step.

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

## Laboratory quantities

Rack calls a quantity's value and range methods (`get_value`, `set_value`,
`get_min_value`, `get_max_value`, `get_default_value`) from its engine
thread too, for a module's parameters (Core MIDI-Map), so outside the
laboratory a Python quantity cannot override them. The laboratory's
{py:class}`rackpy.laboratory.Quantity`, {py:class}`~rackpy.laboratory.ParamQuantity`
and {py:class}`~rackpy.laboratory.SwitchQuantity` may; creating one needs
`enable()`. Rack's own code that calls them (display values and strings,
`reset`, `randomize`, scaled values, `paramsToJson`, `paramsFromJson`,
`setImmediateValue`'s clamping) reaches the Python override synchronously,
and an override's error reaches the Python caller. `super()` runs Rack's
implementation.

Only a laboratory module takes a laboratory parameter quantity, through
`config_param(..., quantity_type=...)` or its `param_quantities`; an ordinary
module raises `LaboratoryError`, because Rack's engine can call its
quantities on its own threads. Were Rack ever to call a laboratory quantity
from one of its threads, RackPy would refuse to run Python there (a
`refused_rack_thread` diagnostic) and Rack's implementation would answer; the
test-hooks build checks exactly that.

## A laboratory MIDI output device

`midi::OutputDevice::sendMessage` runs on Rack's engine threads, when a
module's `process` sends MIDI, so a production device Python configures
only queues what it receives ([MIDI devices](../reference/midi.md#python-drivers-and-devices)).
{py:class}`rackpy.laboratory.MidiOutputDevice` is a device whose
`send_message(message)` a Python subclass overrides. It is never one of
Rack's drivers' devices: `device.connect(output)` points a
`rackpy.raw.midi.Output` you made at it, and Rack's `Output::sendMessage`
(which applies the port's channel) then calls the override synchronously,
for example from a laboratory module's `process` on a bench. Deleting the
device disconnects its ports; a deleted port leaves the device.

## A laboratory MIDI generator

Rack's `dsp::MidiGenerator<N>` turns gates and CV into MIDI and hands each
message to its virtual `onMessage`, which a module calls from `process` on
Rack's engine thread. So `rackpy.raw.dsp.MidiGenerator[N]` implements it
natively, recording the messages for `take_messages()`, and refuses a
subclass that defines `on_message`. A subclass of
`rackpy.laboratory.MidiGenerator[N]` (created after `enable()`) may: Rack's
generator calls the override synchronously, in its own order, with each
message's bytes and frame; `super().on_message(message)` records it, as the
native implementation does; an error reaches the caller of the generator
method that produced the message. Callback messages are owned copies and
may be retained. A native per-instance mark, set by the laboratory
constructor only after checking enablement, also gates dispatch; defining
an inherited callback or monkeypatching a raw generator cannot opt it in.
These are Python-owned, offline generators, never
registered with Rack's engine or MIDI drivers. If native code nevertheless
calls one on a Rack thread, Python is refused (`refused_rack_thread`) and
the native recorder answers; the test-hooks build probes that fallback.
As with laboratory quantities, `enable()` gates creation; disabling the
laboratory is not a revocation of already-created generators.

## Re-entrant virtual calls and `super()`

Native code that calls the same virtual again on the same Python object runs
its Python override again, just as C++ virtual dispatch would. For example,
an `on_message` override that calls a generator method which emits another
message receives that nested message synchronously too. It is not silently
sent to the native recorder. `super().on_message(message)` remains the
explicit, non-virtual recorder call.

For Python subclasses, a native base binding reached through `super()` or
an explicit `Base.method(instance)` runs that **base implementation**, not
the override. This applies across quantities, metadata, history, MIDI ports,
widgets, UI and app controls. Native C++ subclasses (Rack's own, or another
plugin's) still dispatch virtually when called through a base binding.
Previously some bindings relied on pybind11's current-frame heuristic to
avoid recursive `super()` calls; those explicit base calls could redispatch
when made outside the override. They now consistently mean the base method
for Python subclasses. Base implementations can themselves call *other*
virtuals, which still reach Python normally.

A shared per-thread guard allows at most **64 live native-to-Python override
calls** (including argument/result conversion), across trampoline families, to bound C++ frames invisible to
Python's recursion counter. Exceeding it raises `RecursionError` to a Python
caller, or is contained/reported with that family's normal native fallback
when Rack called. Attribute lookup itself is bounded too; lookup errors
preserve pybind11's defaulted-attribute policy (no override, native fallback),
rather than the invocation's error policy. The counter unwinds after an
error, so subsequent calls still work. After overflow, an exhaustion latch
refuses retries until the whole chain unwinds; this also bounds numeric
casters that clear a conversion error and retry. Such return-conversion
errors keep pybind11's normal outer `RuntimeError` instead of exposing the
inner `RecursionError`. Laboratory module callbacks use their existing, separate
200-level guard. Neither guard makes re-entering Rack's engine locks or a
laboratory Bench safe; those operations remain refused. The source audit and validation are recorded in
`planning/trampoline-reentry-validation.md`.

## Measuring laboratory callback costs

The reproducible benchmark is
`tests/laboratory/in_rack_cost/test_callback_cost.py`, driven inside actual
Rack by `tests/laboratory/test_callback_cost.py`. Raw data and exact machine,
compiler command, source hashes, Python version, randomized execution order
and native baseline are retained in
[`benchmarks/rp22-laboratory-cost.json`](../../benchmarks/rp22-laboratory-cost.json).
See [performance](../performance.md#laboratory-callback-cost-rp-22d) for
results and reproduction. Each timed `Bench.step(20000)` invokes one virtual
per frame; it does **not** batch audio across Rack modules to improve timing.

This is an **offline diagnostic**, not production real-time performance:
Python already holds the GIL on the main thread, there is no competing
Python thread, no driver or audio clock, and there are no device deadlines.
It includes Bench scheduling and argument conversion, not just the override
body. A fast median cannot demonstrate bounded latency, absence of GIL stalls,
or any production audio budget in ADR 004.

## Crash-isolated failure and shutdown probes

`tests/laboratory/test_races.py` runs each exception, superclass recursion,
cross-object recursion and close/delete stale-view probe in a separate actual
Rack process with a watchdog. A signal/crash or timeout fails the outer test,
not the outer interpreter. Recursion must raise `RecursionError`, unwind the
native nesting/error-capture state and leave a fresh module usable. Closed or
deleted modules invalidate retained port/voltage views; stale base callback
calls and adding closed modules back to a bench are refused.

The shutdown scenario races a Python worker's rejected `Bench.step` attempts
with main-thread use and the closing hook. It joins that worker while the
interpreter is usable, but deliberately retains the bench/module/override
cycle, laboratory quantity, MIDI generator and device through Rack teardown.
The live-context run also retains a connected MIDI output port (constructing
one requires Rack's context, which the startup/headless run does not have). In both startup/headless and live-context windowed
Rack, live native override holders must cause the documented **safe refusal
to finalize Python**, with a diagnostic and normal Rack exit, never freeing
Python underneath them (ADR 003). A second variant releases those holders in
the closing hook and requires `Py_FinalizeEx returned 0`. These are adversarial
lifetime probes, not a claim to exhaust all schedules or prove sanitizer-clean
behavior.
