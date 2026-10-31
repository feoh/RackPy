# Examples

RackPy has, or will have, an original, runnable example for every feature
that the official VCV Rack tutorial, plugin API guide and plugin template
demonstrate; none of them copies the VCV manual
([ADR 001](decisions/001-licensing-and-identity.md)). Ten exist so far, all
Linux x86_64 in Rack Free {{rack_version}}:

- [Plugin template](../examples/template/README.md):
  `bin/rackpy-plugin`, RackPy's counterpart of Rack's `helper.py`, creates a
  plugin project, generates a module from an SVG panel's components layer, and
  builds a plugin Rack loads under its own slug.
- [Oscillator](../examples/oscillator/README.md):
  the tutorial's sine oscillator with a 1 Hz blink, running as a native kernel
  compiled from a graph, compared in Rack with a C++ reference at 44.1, 48 and
  96 kHz.
- [Configuration](../examples/configuration/README.md):
  parameter displays (plain, volts, percent, hertz on a base-2 scale,
  decibels), a labelled switch, a button, descriptions, port and light names,
  bypass routes, a custom bypass that runs a second native kernel
  ([processing audio](how-to/dsp.md#reset-sample-rate-bypass-and-failures)),
  and factory presets. In Rack, every tooltip and frame matches the same
  modules written in C++.
- [State and events](../examples/state-and-events/README.md):
  a sequencer whose range is saved in the patch, whose pattern is a file in
  its patch storage and whose default length is a plugin setting
  ([state](how-to/state.md)); Initialize and Randomize handlers; and a
  laboratory script that handles every module event, the sample rate's
  included, in Python, offline.
- [Menus](../examples/menus/README.md): a pitch
  source set entirely from its context menu, with one of each of Rack's menu
  helpers ([menus](reference/ui.md#menu-helpers)).

- [Dark panels](../examples/dark-panels/README.md)
  and [Drawing](../examples/drawing/README.md): a
  light and a dark panel; NanoVG shapes, text, a self-lit layer and a cached
  framebuffer. The [walkthrough](how-to/draw-panels.md) shows them in
  screenshots of Rack's own frames.

- [Polyphony](../examples/polyphony/README.md),
  [DSP](../examples/dsp/README.md) and
  [Expanders](../examples/expanders/README.md):
  plugins that ship their own [C++ kernel libraries](how-to/dsp.md#write-a-kernel-in-c):
  a polyphonic oscillator one channel at a time and with `float_4`, a biquad
  filter from Rack's `rack::dsp`, and modules that exchange expander messages
  or read the module beside them.

Every sample of the tutorial, the API guide and the plugin template has a
tested example; the rest are shell commands and notes that the guides
document (RP-37).

The plan, the required layout of each example and the list of features are in
[examples/README.md](../examples/README.md). The 77 official samples, with
their source sections and the RackPy task that covers each, are in
[upstream/examples.json](../upstream/examples.json).

## Provenance and licences

Each example is original work under GPL-3.0-or-later. Its `PROVENANCE.md`
says which official samples it covers, how it differs from them, and which
SVG, font and code sources it uses:
[template](../examples/template/PROVENANCE.md),
[oscillator](../examples/oscillator/PROVENANCE.md),
[configuration](../examples/configuration/PROVENANCE.md),
[state and events](../examples/state-and-events/PROVENANCE.md),
[menus](../examples/menus/PROVENANCE.md),
[dark panels](../examples/dark-panels/PROVENANCE.md),
[drawing](../examples/drawing/PROVENANCE.md),
[polyphony](../examples/polyphony/PROVENANCE.md),
[DSP](../examples/dsp/PROVENANCE.md) and
[expanders](../examples/expanders/PROVENANCE.md).
[Licensing](licensing.md) covers RackPy's own licence, the bundled Python's
and the VCV manual's.

## Runnable snippets

Every code listing on this site that claims to run is a file in
`docs/snippets/` that the test suite runs, in Rack where it needs Rack
([conventions](contributing-docs.md)), and so is every example. This one
prints what runs inside Rack:

```{rackpy-example} rack_runtime.py
:caption: Which RackPy and Python run inside Rack
```
