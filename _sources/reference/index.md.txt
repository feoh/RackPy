# Reference

:::{admonition} Bound, not yet verified
:class: warning

The binding manifest catalogs Rack's public SDK declarations, except
Rack-internal ones; coverage and test status vary, and none is marked `verified`
yet. CI has built and run install candidates on Linux x86_64, macOS x86_64/arm64,
and Windows x86_64, but this is not a completeness claim or a published release.
The [coverage report](coverage.md) has the totals, and each declaration's status
comes from the binding manifest that CI checks.
:::

```{toctree}
:maxdepth: 1

coverage
crosswalk
mapping
api-stability
```

## By Rack namespace

```{toctree}
:maxdepth: 1

values
engine-components
engine-module
engine
history
midi
audio
services
simd
dsp
plugin
widgets
ui
window
app
componentlibrary
widget-trees
```

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
callback-paths
```

## The rackpy package

```{toctree}
:maxdepth: 1

python/rackpy/index
```
