# Reference

:::{admonition} Incomplete
:class: warning

RackPy binds no Rack API yet. These pages list what it **must** bind. When the
binding manifest exists (RP-11), each entry will show its Python name, status,
tests and documentation, generated from the same manifest that CI checks.
:::

## The SDK surface, by family

Generated from the [SDK inventory](surface-schema.md) of Rack
{{rack_version}}. Every public declaration, macro and template that
`#include <rack.hpp>` exposes is a row; private Rack internals are listed as
excluded, with the evidence in the inventory.

```{toctree}
:maxdepth: 1

surface/entry-point
surface/common
surface/engine
surface/plugin
surface/widgets
surface/window
surface/dsp
surface/audio-midi
surface/services
surface/history
```

## About the inventory

```{toctree}
:maxdepth: 1

surface-summary
surface-schema
callback-matrix
```

## The rackpy package

```{toctree}
:maxdepth: 1

python/rackpy/index
```
