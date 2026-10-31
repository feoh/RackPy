# Reference

:::{admonition} Bound, not yet verified
:class: warning

On Linux x86_64, RackPy binds every public declaration of Rack's SDK that
exists there, except Rack-internal ones, and each is tested; none is marked `verified`
yet, and macOS and Windows are not built. The [coverage report](coverage.md)
has the totals, and each declaration's status comes from the binding manifest
that CI checks.
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
