# Reference

:::{admonition} Incomplete
:class: warning

RackPy binds only part of the Rack API. These pages list what it **must** bind,
with each declaration's status in the binding manifest that CI checks. The
[coverage report](coverage.md) has the totals.
:::

```{toctree}
:maxdepth: 1

coverage
```

## Bound so far

```{toctree}
:maxdepth: 1

values
engine-components
engine-module
engine
history
midi
audio
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
```

## The rackpy package

```{toctree}
:maxdepth: 1

python/rackpy/index
```
