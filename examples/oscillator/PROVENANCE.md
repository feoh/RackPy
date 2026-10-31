# Provenance: oscillator

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `tutorial-06` | Oscillator and blink phase state | the graph's two phasors, in `pythonpath/rackpyoscillator_dsp.py` |
| `tutorial-07` | Sine oscillator `process()` with a 1 Hz blink light | `oscillator_graph()`, compiled to a native kernel in `oscillator.py`'s `configure()` |

The content hashes are recorded with the samples in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Their dispositions there name these files and their tests.

## How it was made

The project and the first `oscillator.py` were generated with
`bin/rackpy-plugin create` and `add-module` from `res/Oscillator.svg` (see
[the template](../template/PROVENANCE.md) for that tool's provenance). The
module was then edited by hand: the controls' ranges, names and display, the
light's name, the native kernel, a larger knob, and the module docstring.
`rackpyoscillator_dsp.py` is new.

This is an original example demonstrating the tutorial's feature, written from
the pinned Rack headers and RackPy's API. No VCV manual code or text is copied
or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C). `native/tests/oscillator_reference.cpp` is likewise original.

## Original assets

`res/Oscillator.svg` is original RackPy artwork (GPL-3.0-or-later), not derived
from VCV's or Fundamental's panels. The knob, jacks, light and screws are Rack's
component library, drawn from Rack's own resources at runtime.
