# Examples

RackPy has, or will have, an original, runnable example for every feature
that the official VCV Rack tutorial, plugin API guide and plugin template
demonstrate; none of them copies the VCV manual
([ADR 001](decisions/001-licensing-and-identity.md)). Two exist so far, both
Linux x86_64 in Rack Free {{rack_version}}:

- [Plugin template](https://github.com/feoh/RackPy/tree/main/examples/template):
  `bin/rackpy-plugin`, RackPy's counterpart of Rack's `helper.py`, creates a
  plugin project, generates a module from an SVG panel's components layer, and
  builds a plugin Rack loads under its own slug.
- [Oscillator](https://github.com/feoh/RackPy/tree/main/examples/oscillator):
  the tutorial's sine oscillator with a 1 Hz blink, running as a native kernel
  compiled from a graph, compared in Rack with a C++ reference at 44.1, 48 and
  96 kHz.

The guide's configuration, lifecycle, menu, panel, drawing, polyphony, SIMD,
DSP and expander examples are planned (RP-34 to RP-36).

The plan, the required layout of each example and the list of features are in
[examples/README.md](../examples/README.md). The 77 official samples, with
their source sections and the RackPy task that covers each, are in
[upstream/examples.json](../upstream/examples.json).

Every code listing on this site that claims to run is a file in
`docs/snippets/` that the test suite runs, in Rack where it needs Rack
([conventions](contributing-docs.md)). Today there is one:

```{rackpy-example} rack_runtime.py
:caption: Which RackPy and Python run inside Rack
```
