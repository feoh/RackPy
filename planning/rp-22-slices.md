# RP-22 bounded implementation slices

RP-22 (`tk-rackpy-22-implement-explicit-non-real-time-pytho-4c57ff`) gives
Python a real execution path for the module callbacks Rack runs on its
engine and audio driver threads, without ever running Python there. It is
split into children, each closed with a commit on `main`. Routine decisions
follow the owner's standing instruction to take the recommended option; the
ones below were also approved by the supervisor on 2026-10-26.

## Step 1: feasibility (decided)

A public-API-only host cannot run a Python `process()` inside Rack's real
engine. Rack's engine threads call `Module::doProcess`, and what a host would
need to drive or imitate the engine is `PRIVATE` in the pinned SDK
(`include/engine/Module.hpp`, `include/engine/Engine.hpp` at `061ccf63`):

- `PRIVATE void doProcess(const ProcessArgs& args);` (Module.hpp:470)
- `PRIVATE void setBypassed(bool bypassed);` (Module.hpp:466)
- `PRIVATE void setExpanderModule(Module* module, uint8_t side);` (Module.hpp:473)
- `PRIVATE Engine();` and `PRIVATE ~Engine();` (Engine.hpp:26-27)

Inside the real engine a Python `process()` would run on engine worker
threads, where RackPy refuses Python, or stall them on the GIL. **Decision:**
the laboratory is an offline **Bench** on the main thread that steps RackPy
modules with the public semantics of Rack 2.6.6's `Engine.cpp`
(`stepFrame`, `stepFrameCables`, message flips, add/remove/cable/bypass/
sample-rate events). Laboratory modules never enter Rack's engine and cannot
be created from a model or a patch. Fidelity is claimed only as far as the
tests compare a Bench with Rack's engine. A Bench cannot reproduce engine
worker threads, the audio driver thread, master-module timing, parameter
smoothing, CPU meters, plug lights or Rack's random module IDs; those are
listed as gaps in `docs/explanation/callbacks.md`.

| Slice | Scope |
| --- | --- |
| 22A | Bench, `rackpy.laboratory.enable()`, `process`/`processBypass`/`step` trampolines, `ProcessArgs`, normal-mode refusals (7 records) |
| 22B | Synchronous laboratory overrides of module events and JSON, the sample-rate, master and expander-change events (7 records, 18 annotated) |
| 22C | Laboratory `Quantity`/`ParamQuantity` value methods; the RP-12 base-Quantity inconsistency; `midi::OutputDevice::sendMessage` (5 records, 6 reconciled) |
| 22D | Callback inventory (all 434 virtuals), measured laboratory costs, subprocess races, RP-22 closing validation |
| 22E | `dsp::MidiGenerator::onMessage` laboratory override (after RP-18D) |

**RP-22A — Bench, enablement and process trampolines**
(`tk-rackpy-22a-laboratory-bench-explicit-enablement--8e9285`). Records:
`Module::process`, `processBypass`, `step()`, `ProcessArgs` and its three
fields. Validation: [rp-22a-validation.md](rp-22a-validation.md).

- **Enablement.** A process-wide flag, off at start-up, set only by
  `rackpy.laboratory.enable()` from trusted code (logged in Rack's log).
  Creating laboratory modules or benches, adding to or stepping a bench, all
  raise `LaboratoryError` (a new `rackpy.errors` class) while it is off.
- **Laboratory module.** A `rackpy::module::Module` subclass flagged
  `laboratory`; `Engine.add_module`, the test hooks' adoption and `set_kernel`
  refuse it. `rackpy.laboratory.ModuleClass` extends `rackpy.plugin.ModuleClass`;
  `factory()` refuses a laboratory class, so no model or patch can make one.
- **Trampolines.** Python overrides are held as `PyRef`s (visible to the
  module handle's GC traversal); `super()` calls Rack's base
  implementations. An override's error goes to the Python caller
  (`ErrorCapture`), never through Rack's code. Nesting through Rack's code is
  bounded at 200 levels (`RecursionError`).
- **Reentry.** A callback cannot step, edit or clear any bench, mirroring
  Rack's non-recursive engine lock (`LaboratoryError`).
- **Normal mode.** `rackpy.plugin.ModuleClass` refuses `process`,
  `process_bypass`, `step`, `on_sample_rate_change`, `on_set_master`,
  `on_unset_master` and `on_expander_change` at class definition, naming
  native kernels and the laboratory.

**RP-22B — Synchronous laboratory events and JSON**
(`tk-rackpy-22b-synchronous-laboratory-module-events--23893f`). Records
moved to `implemented`: `onSampleRateChange` (both overloads),
`onSetMaster`, `onUnsetMaster`, `onExpanderChange`, `ExpanderChangeEvent`
and its `side`. The twelve UI-thread event records and the six JSON records
gain a laboratory override path in their notes. Validation:
[rp-22b-validation.md](rp-22b-validation.md).

- **Override semantics.** Laboratory event and JSON methods replace Rack's
  implementation; `super()` runs the RackPy module's own (Rack's behaviour,
  the lifecycle snapshot, the data handlers). This differs on purpose from
  `rackpy.plugin.ModuleClass`, whose handlers run after Rack's behaviour.
- **Names.** Rack's `Module::toJson`/`fromJson` are `module_to_json`/
  `module_from_json` (as `Engine::moduleToJson` calls them), because
  `ModuleClass.to_json`/`from_json` already mean the custom-data handlers.
  The deprecated no-argument events have no separate override; `super()`
  reaches them (RP-16C's convention).
- **Bench.** Adds Rack's `setMasterModule`, `resetModule`, `randomizeModule`,
  `prepareSave(Module)`, removal order (master, then expanders) and
  `stepBlock`'s expander updates; `set_expander` sets an expander's module
  ID, as Rack's rack and patch loading do. Rack's `setExpanderModule` is
  `PRIVATE`, so the bench writes the public `Expander::module` and calls the
  public `onExpanderChange`.
- **Production.** RackPy modules now snapshot `onExpanderChange` (lock-free,
  Python-free), so the bench's event order can be compared with Rack's
  engine. `ModuleClass.create(model)` names a laboratory module's model for
  Rack's `toJson`; Rack still never creates or runs it.

**RP-22C — Laboratory quantities and MIDI output device**
(`tk-rackpy-22c-laboratory-quantity-value-methods-rec-c2b64e`). Records:
the five `ParamQuantity` value and range methods move to `implemented`; the
five base `Quantity` ones and `midi::OutputDevice::sendMessage` keep
`implemented` and gain their laboratory path. Validation:
[rp-22c-validation.md](rp-22c-validation.md).

- **RP-12 inconsistency, resolved.** The base `Quantity` value records were
  `implemented` with a native path only, while the same methods on
  `ParamQuantity` stayed `planned` for want of a laboratory path. Both now
  have the same contract: native production path, laboratory Python path
  (`rackpy.laboratory.Quantity`, `ParamQuantity`, `SwitchQuantity`).
- **Mechanism.** The shared `QuantityTrampoline` dispatches the value
  methods to Python only for an object the laboratory marked as it was
  created (after `enable()`); every other quantity keeps exactly its native
  path. Laboratory parameter quantities are accepted only by laboratory
  modules' slots, so Rack's engine thread cannot reach them; if it did,
  `callPython` would refuse and Rack's implementation would answer.
- **Names.** The five methods are bound on `rackpy.raw.engine.ParamQuantity`
  as well (they were inherited from `Quantity`), so the records name the
  SDK declaration, as for the class's UI virtuals.
- **`sendMessage`, reconciled.** RP-29C's queueing `PythonOutputDevice` is
  the native production path; the laboratory path is
  `rackpy.laboratory.MidiOutputDevice`, a subclassable device never
  registered with Rack's drivers. Python-made output ports connect to it
  directly, and Rack's `Output::sendMessage` reaches the override.

**RP-22E — Laboratory MidiGenerator::onMessage**
(`tk-rackpy-22e-laboratory-python-override-of-dsp-mid-5172f2`). Record:
`dsp::MidiGenerator::onMessage` moves to `implemented`. Validation:
[rp-22e-validation.md](rp-22e-validation.md). The supervisor named
`native/bindings/dsp/midi.cpp` and `src/rackpy/raw/_dsp.py` as the
implementation seams; the laboratory facade, stubs, tests and mapping are
updated with them. A narrow, test-hooks-only Rack-thread probe was also
approved. `docs/reference/dsp.md` (RP-19's) is not edited here, so the
explanation page carries the description.

- **Trampoline.** Each `MidiGenerator_N` gets a pybind11 alias whose
  `onMessage` calls a Python `on_message` override. Ordinary generators
  keep no `on_message` attribute (an RP-18D test checks it); the laboratory
  class defines `on_message`, which calls RackPy's recording implementation
  non-virtually through the private `_on_message`, so `super().on_message`
  records. Messages passed to Python are owned copies, safe to retain.
- **Gating.** `rackpy.raw.dsp` still refuses `on_message` in any subclass
  except `rackpy.laboratory.MidiGenerator[N]` classes, whose instances need
  `enable()`. A native per-instance mark gates the trampoline too: inherited
  mixin callbacks, late monkeypatching and a raw-base constructor cannot
  bypass enablement. Rack never holds these Python-owned generators; a call
  without the GIL goes through `callPython`, which refuses Rack's threads.

- **Known dispatch divergence.** The shared `overrideOrVoid` helper uses
  pybind11's same-object/same-name frame check. Reentry through a different
  native generator method from `on_message` records the nested message,
  rather than invoking Python again as a C++ override would. This is pinned
  by a test, not fixed locally; the cross-cutting follow-up is
  `tk-rackpy-trampolines-suppress-same-object-re-entra-0abce9`.

**RP-22D — Inventory, measurements and closing probes**
(`tk-rackpy-22d-callback-inventory-measured-laborator-f8465c`). Validation:
[rp-22d-validation.md](rp-22d-validation.md). The generated
[execution inventory](../docs/reference/callback-paths.md) joins every one of
the 434 public virtual declarations to its domain, Python names and semantics
or full-release blocker. No status is promoted to verified. Tests detect drift;
raw laboratory cost measurements are separate from production budgets; failure,
recursion, stale-view and shutdown probes use isolated actual-Rack subprocesses.

The audit found 33 native-configured/queued driver/device callbacks without a
synchronous laboratory Python path, plus 21 borrowed-host-view UI callbacks
without Python subclass trampolines. Supervisor-approved status corrections
keep them `planned`, retaining native-path names and evidence. Follow-ups:

- **RP-22F** (`tk-rackpy-22f-laboratory-python-paths-for-audio-and-65ccf3`):
  laboratory Python paths for the 33 driver/device callbacks; blocks RP-22.
- **RP-28F** (`tk-rackpy-28f-python-subclass-trampolines-for-plugw-154b61`):
  UI-safe subclass trampolines for the 21 host-view-only callbacks.

These gaps and the shared-helper reentry task above remain full-release
blockers; closing validation does not imply that RP-22 or full coverage is complete.
