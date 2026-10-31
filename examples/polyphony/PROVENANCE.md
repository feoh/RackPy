# Provenance: polyphony

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `guide-24` | A phase per channel | `Voice voices[16]` and `float_4 phases[4]` in `kernels/polyphony.cpp` |
| `guide-25` | Polyphonic sine oscillator | `polyphony.Scalar` |
| `guide-26` | Per-channel engine struct | `Voice` |
| `guide-27` | Per-channel engine access | `voices[c].process(...)` |
| `guide-28` | The scalar loop SIMD replaces (illustration) | `rackpypolyphony_teaching.four_at_a_time` |
| `guide-29` | `float_4` phase array | `float_4 phases[4]` |
| `guide-30` | SIMD polyphonic sine with `float_4` | `polyphony.Simd` |

The content hashes are recorded with the samples in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Their dispositions there name these files and their tests.

## Corrections of the manual

`guide-25` passes the whole phase array to `std::sin`, and `guide-30`
declares `float_t freq` and calls `simd::sin` on the whole array; the pinned
headers need `phase[c]`, `float_4` and `phase[c / 4]`, as this example has
([errata](../../docs/licensing.md#errata-found-while-classifying)).

## How it was made

The project and the first `polysine.py` were generated with `bin/rackpy-plugin
create` and `add-module` from `res/PolySine.svg` (see
[the template](../template/PROVENANCE.md) for that tool's provenance), then
edited by hand to choose a kernel per model. `kernels/polyphony.cpp`,
`rackpypolyphony_teaching.py` and `native/tests/polyphony_reference.cpp` are
new.

This is an original example demonstrating the guide's features, written from
the pinned Rack headers and RackPy's API. No VCV manual code or text is copied
or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C).

## Original assets

`res/PolySine.svg` is original RackPy artwork (GPL-3.0-or-later), not derived
from VCV's or Fundamental's panels. The knob, jacks and screws are Rack's
component library, drawn from Rack's own resources at runtime.
