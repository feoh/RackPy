# Provenance: state-and-events

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `guide-08` | `onReset`, `onRandomize`, `onSampleRateChange` | `on_reset` and `on_randomize`; the kernel's native sample rate; `laboratory/events_on_a_bench.py` |
| `guide-09` | `dataToJson`, `dataFromJson` | `to_json`, `from_json` and `rackpystateevents_core.State` |
| `guide-10` | Patch storage in `onAdd` and `onSave` | `on_add` and `on_save` with `Module.patch_storage()` |
| `guide-11` | Plugin settings | `plugin.set_settings_handlers` and `rackpystateevents_core.Settings` |
| `migrate2-05` | Disabling reset and randomize per parameter | LENGTH's `reset_enabled` and `randomize_enabled` |
| `migrate2-06` | Explicit parameter values in `onReset` and `onRandomize` | LENGTH set by both handlers |
| `migrate2-11` | Patch storage in `onAdd` and `onSave` | as `guide-10` |

The content hashes are recorded with the samples in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Their dispositions there name these files and their tests.

## Corrections of the manual

- `guide-08` overrides `onRandomize` with a `ResetEvent`; the pinned headers
  take a `RandomizeEvent`, as `migrate2-06` has, and so does this example.
- `guide-10` calls `getPatchStorageDirectory()` in `onAdd` where `migrate2-11`
  calls `createPatchStorageDirectory()`. Reading needs no directory, so
  `on_add` computes the path without creating it (`patch_storage(create=False)`)
  and `on_save` creates it as it writes.

## How it was made

The project and the first `sequencer.py` were generated with
`bin/rackpy-plugin create` and `add-module` from `res/Sequencer.svg` (see
[the template](../template/PROVENANCE.md) for that tool's provenance), then
rewritten as a `ModuleClass`. `rackpystateevents_core.py` and
`laboratory/events_on_a_bench.py` are new.

This is an original example demonstrating the guide's features, written from
the pinned Rack headers and RackPy's API. No VCV manual code or text is copied
or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C).

## Original assets

`res/Sequencer.svg` is original RackPy artwork (GPL-3.0-or-later), not derived
from VCV's or Fundamental's panels. The knob, jacks, light and screws are
Rack's component library, drawn from Rack's own resources at runtime.
