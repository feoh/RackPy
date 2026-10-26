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
| 22B | Synchronous laboratory overrides of module events and JSON, the sample-rate, master and expander-change events |
| 22C | Laboratory `Quantity`/`ParamQuantity` value methods; the RP-12 base-Quantity inconsistency; `midi::OutputDevice::sendMessage` |
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

**RP-22B, 22C, 22D, 22E** are described in their Witan tasks and are
recorded here as they land.
