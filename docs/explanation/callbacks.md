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
coverage. The 33 audio and MIDI driver/device virtuals (`audio::Driver`,
`audio::Device`, `midi::Driver`, `midi::Device::getName`, `Input` and
`InputQueue::onMessage`) have a native production path, natively configured
answers or queued notifications, plus a laboratory Python path since RP-22F
([laboratory audio and MIDI drivers](#laboratory-audio-and-midi-drivers)). The five `audio::Port` stream
virtuals have synchronous Python overrides on a **Python-driven device**,
never on Rack's driver threads; queued Rack notifications are a different
path ([audio](../reference/audio.md)).

The audit also kept **21 UI virtuals** on `PlugWidget`, `RackScrollWidget`,
`RackWidget` and `Scene` as blockers, because only borrowed host views
reached them. RP-28F
(`tk-rackpy-28f-python-subclass-trampolines-for-plugw-154b61`) found that no
plugin can override them: the last three have PRIVATE constructors, and only
`CableWidget`'s constructor makes a usable `PlugWidget`. By a decision
the owner approved on 2026-10-26 ([coverage policy 11](../coverage.md#completeness-policy)),
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

The five deprecated no-argument Module events have distinct `on_add_legacy`,
`on_remove_legacy`, `on_reset_legacy`, `on_randomize_legacy`, and
`on_sample_rate_change_legacy` override slots. Defining one or explicitly
calling its base warns. Rack's default modern event dispatches the legacy
virtual; a modern override suppresses it unless it calls `super()`. This
internal dispatch alone does not issue a deprecation warning.

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
device disconnects its ports; a deleted port leaves the device. It also has
a configured `name` and an overridable `get_name`, and a
[laboratory MIDI driver](#laboratory-audio-and-midi-drivers) can hold it.

## Laboratory audio and MIDI drivers

Rack calls an audio or MIDI driver's and device's virtuals from its driver
threads, and Core's Audio module reads a device's channels and rate on the
audio thread. So in production, a driver Python configures
(`rackpy.raw.audio.PythonDriver`, `rackpy.raw.midi.PythonDriver`) answers
natively, and a Python `Input` gets its messages queued for the UI thread
([audio](../reference/audio.md#python-drivers-and-devices),
[MIDI](../reference/midi.md#python-drivers-and-devices)). The laboratory classes let
a Python subclass override these virtuals instead, offline:

| Class | Rack virtuals it may override |
| --- | --- |
| {py:class}`rackpy.laboratory.AudioDriver` | every `audio::Driver` virtual: `get_name`, `get_device_ids`, `get_default_device_id`, `get_device_name`, `get_device_num_inputs`, `get_device_num_outputs`, `subscribe`, `unsubscribe` |
| {py:class}`rackpy.laboratory.AudioDevice` | every `audio::Device` virtual: `get_name`, channel counts, sample rates and block sizes and their setters, `subscribe`, `unsubscribe` |
| {py:class}`rackpy.laboratory.MidiDriver` | every `midi::Driver` virtual, for inputs and outputs |
| {py:class}`rackpy.laboratory.MidiInputDevice`, {py:class}`~rackpy.laboratory.MidiOutputDevice` | `midi::Device::getName` (and the output's `send_message`, above) |
| {py:class}`rackpy.laboratory.MidiInput`, {py:class}`~rackpy.laboratory.MidiInputQueue` | `Input::onMessage`, `InputQueue::onMessage` |

Rack never lists a laboratory driver: nothing registers it, so Rack's menus,
patches and driver threads cannot find it. `driver.connect(port)` points a
`rackpy.raw.audio.Port` or `rackpy.raw.midi` port you made at it. From then
on, **Rack's own port code calls the overrides synchronously**, on the main
thread, where it would call a C++ driver's: `Port::setDeviceId` subscribes
through `subscribe` (and the driver's `super()` calls the device's
`subscribe`), `Port::getSampleRate` asks the device, `Port::toJson` takes
the device's name, `Input::getDefaultDeviceId` asks the MIDI driver, and
Rack's `MidiDriverChoice` and `MidiDeviceChoice` widgets show the names they
return. Rack's `InputDevice::onMessage`, called by a `MidiInputDevice`'s
`push_message` or by Rack's Loopback driver when Python sends, calls a
laboratory input's `on_message` synchronously. An override's error reaches
the Python caller of the port method, through Rack's code.

One virtual has no Rack caller for a driver Rack does not list:
`audio::Driver::getDefaultDeviceId`, which Rack 2.6.6 calls only from
`audio::Port::setDriverId` (`src/audio.cpp`, line 116), after finding the
driver by its registered ID. `connect` therefore **repeats** what
`setDriverId` does once it has found a driver: the port leaves its device,
takes the driver, asks `get_default_device_id()` and subscribes to that
device through Rack's `Port::setDeviceId`. That call comes from RackPy's
reproduction of `setDriverId`, not from Rack's own function, as a
[bench](#the-laboratory-bench) reproduces Rack's engine. Registering the
driver with Rack's public `addDriver` would be irreversible (Rack has no
public removal) and would expose it to Rack's UI and driver threads. MIDI
`connect` repeats `setDriverId` the same way, through the port's virtuals;
the MIDI default-device virtuals also have Rack's own callers.

`super()` is the native answer, which is the configured behaviour, as for
the production drivers: the driver's name, the devices given to
`add_device` (or `add_input_device`/`add_output_device`) and the first as
the default; a device's `name`, `num_inputs`, `sample_rate` and so on, whose
setters record requests for `take_requests()`; Rack's `InputQueue` queue.
Base methods called from Python run that native answer, never the override.
A driver keeps its devices alive until `remove_device`, so a device that
`subscribe` returns outlives the call; `subscribe` must return one of the
driver's own devices, or `None`, else `OwnershipError`. Callbacks receive
the ports' own Python objects (the lifetime-tracked `rackpy.raw` ports, never
addresses) and owned `Message` copies.

The rules that keep it contained:

- **Explicit enablement.** Creating any of these classes needs
  {py:func}`~rackpy.laboratory.enable`, and so does `connect`. A
  `MidiInput`'s constructor sets a native per-instance mark after checking
  enablement; an ordinary `rackpy.raw.midi.Input` subclass, or one that
  bypasses the laboratory constructor, keeps the queued production path.
  Every `MidiInputQueue` is a laboratory one: its only constructor checks
  enablement. It is a `rackpy.raw.midi.Input`, but not a
  `rackpy.raw.midi.InputQueue`, which is final.
- **Never Rack's threads.** Only ports Python made reach these objects, and
  Python calls their methods on the main thread. Were a Rack thread ever to
  call one, RackPy would run no Python (a `refused_rack_thread` diagnostic):
  the native configured answer would come back, Rack's queue would take an
  `InputQueue`'s message, and a `MidiInput`'s message would be queued for
  the UI thread as for any Python input. The test-hooks build checks every
  virtual that way.
- **Teardown runs no Python.** When a port is released (deleted, or Rack's
  context closing), Rack's `~Port` and `setDeviceId(-1)` unsubscribe through
  the driver natively, and the port is never handed to Python. A released
  port also leaves every laboratory device natively, so an override that
  left a subscription inconsistent leaves no dangling port behind. When a
  device is removed or deleted, or a driver deleted, every Python port using
  it leaves it and points nowhere; a device view retained from a callback
  then raises `StaleHandleError`. A driver detaches its ports before it
  lets its devices go, so a device's `__del__` or weakref callback that
  asks a port about its driver finds none.
- **Teardown happens on the main thread only.** Rack's UI reads a port's
  driver and device on the main thread without the GIL, so a laboratory
  driver or device is never destroyed anywhere else. If its last reference
  goes on a Python thread, its Python object goes there, but the native
  driver or device (which answers Rack's calls natively meanwhile) is
  destroyed at the next UI step, or at context close, and its ports point
  nowhere from then on. (Python-made ports dropped on another thread are
  leaked with a warning, as before.)
- **Kept alive while Rack's port code uses them.** Rack's `Port::setDeviceId`
  unsubscribes and then subscribes through the same driver, so an override
  of `subscribe` or `unsubscribe` that drops the last reference to its
  driver or device cannot free it under Rack: whenever such an override
  runs, every laboratory driver and device is kept alive until the Python
  port call (`set_device_id`, `connect`, ...) returns, or, when Rack's own
  code called (its menu items), until the next UI step.
- **No subscription changes during a delivery.** A laboratory input's
  `on_message` runs inside Rack's `InputDevice::onMessage`, which is
  iterating the device's subscribers. Until the Python call that delivered
  the message (`push_message`, `send_message` through Rack's Loopback
  driver, ...) returns, anything that would change a subscriber set raises
  `LaboratoryError`: a port's `set_device_id`, `set_driver_id`, `reset` and
  `from_json`, making a MIDI port, `connect`, the drivers' and devices'
  `subscribe_*`/`unsubscribe_*`, and removing a device, and Rack's own code
  that would change a Python port's subscription (its MIDI menu items,
  through `Port::setDeviceId`). A port, driver or device that Python is
  deallocating (its reference count is 0, for example while its attributes
  are cleared) gets no delivery and is never kept alive. Objects dropped
  meanwhile (the delivered port itself, a laboratory driver or device) are
  kept alive and go when that call returns; Rack's own main-thread
  deliveries (its computer keyboard driver) release them at the next UI
  step, and count as a delivery until Rack's loop returns.

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

An object Python is already deallocating (its last reference gone, for
example while a weak reference's callback runs) has no override: in every
trampoline family, the native implementation answers instead, because calling
the Python method would bring the dying object back. During `__del__` an
object is still alive, so its overrides run as usual.

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
