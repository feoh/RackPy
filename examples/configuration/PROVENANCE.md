# Provenance: configuration

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `guide-01` | Port and light labels | `config_input`, `config_output`, `config_light` in `shaper.py` |
| `guide-02` | `configParam` display scaling: linear, unit, percent, base-2 exponential, dB | the Shaper's five knobs |
| `guide-03` | `configSwitch` with labelled positions | the Shaper's Shape switch |
| `guide-04` | `configButton` | the Shaper's Mute while held button |
| `guide-05` | Param and output descriptions | the Tremolo rate's and the outputs' `description` |
| `guide-06` | Bypass routes with `configBypass` | `config_bypass(SIGNAL, SHAPED)` |
| `guide-07` | Custom `processBypass` | the Splitter's `set_bypass_kernel` |
| `presets-01` | Factory preset directory layout | `presets/Shaper/` |
| `presets-03` | Preset file names and ordering | `01_` to `03_` prefixes |
| `presets-04` | A preset's params | each `.vcvm` file |
| `migrate2-04` | `getParamQuantity()` | `get_param_quantity` |
| `migrate2-08` | Port and light labels | as `guide-01` |
| `migrate2-09` | `configButton` and `configSwitch` | as `guide-03` and `guide-04` |
| `migrate2-10` | Bypass routes | as `guide-06` |

The content hashes are recorded with the samples in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Their dispositions there name these files and their tests.

## How it was made

The project and the first `shaper.py` and `splitter.py` were generated with
`bin/rackpy-plugin create` and `add-module` from the panels (see
[the template](../template/PROVENANCE.md) for that tool's provenance), then
edited by hand: the controls' ranges, names and displays, descriptions,
switch and button components, bypass, and kernels. `rackpyconfiguration_dsp.py`
and the presets are new.

This is an original example demonstrating the guide's features, written from
the pinned Rack headers and RackPy's API. No VCV manual code or text is copied
or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C). `native/tests/controls_reference.hpp`, the C++ comparison, is
likewise original.

## Original assets

`res/Shaper.svg` and `res/Splitter.svg` are original RackPy artwork
(GPL-3.0-or-later), not derived from VCV's or Fundamental's panels. The knobs,
switch, button, jacks, light and screws are Rack's component library, drawn
from Rack's own resources at runtime. The presets are original
(GPL-3.0-or-later).
