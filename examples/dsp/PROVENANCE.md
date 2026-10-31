# Provenance: dsp

## Sources covered

No single manual sample: this example demonstrates, with Rack's own
`rack::dsp` classes, how a RackPy plugin ships native DSP beyond RackPy's
built-in kernels, as task RP-36 asks. It is not one of Fundamental's modules
and is not presented as an official sample.

## How it was made

The project and the first `filter.py` were generated with `bin/rackpy-plugin
create` and `add-module` from `res/Filter.svg` (see
[the template](../template/PROVENANCE.md) for that tool's provenance), then
edited by hand. `kernels/dsp.cpp` and `rackpydsp_teaching.py` are new, written
from the pinned Rack headers and RackPy's API. No VCV manual code or text is
copied or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C).

## Original assets

`res/Filter.svg` is original RackPy artwork (GPL-3.0-or-later), not derived
from VCV's or Fundamental's panels. The knobs, jacks and screws are Rack's
component library, drawn from Rack's own resources at runtime.
