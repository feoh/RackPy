# RackPy

**Python for VCV Rack plugins: expressive at the surface, native where timing matters.**

:::{admonition} Development status
:class: important

RackPy is in **early development**. There is no public release; the owner has
approved an unpublished Linux x86_64 preview for local testing. It builds from
source and loads into Rack Free 2.6.6, while public API coverage remains partial
([coverage report](reference/coverage.md)). CI has built and run candidate
install checks on Linux x86_64, macOS x86_64/arm64, and Windows x86_64, but that
is not a support or completeness claim. See the [Linux preview instructions](installation/linux.md).
:::

RackPy lets you write Rack plugins in idiomatic Python, running inside Rack,
while the audio path stays native. The goal is the complete supported public
plugin API, proved symbol by symbol; see the [coverage contract](coverage.md).

## Versions this documentation describes

| Component | Version |
| --- | --- |
| RackPy | {{rackpy_version}} (local Linux preview; unpublished) |
| Rack SDK | {{rack_version}}, commit {{rack_commit}} |
| Bundled Python | CPython {{python_version}} ({{python_build}}) |
| Binding library | pybind11 {{pybind11_version}} |
| Platforms | Linux x86_64 from source; install candidates verified in CI on Linux x86_64, macOS x86_64/arm64, Windows x86_64; no published artifacts |

## Where to go

- **New here?** The [quickstart](quickstart.md) builds RackPy and makes a
  plugin of your own; the [oscillator tutorial](tutorials/oscillator.md) makes
  it play. [Installation](installation.md) says what exists on each platform.
- **Something went wrong?** [Troubleshooting](troubleshooting.md) explains the
  log messages.
- **Doing a specific job?** The how-to guides cover
  [plugins and models](how-to/plugins.md), [state](how-to/state.md),
  [processing audio](how-to/dsp.md), [graphs](how-to/graphs.md) and
  [drawing panels](how-to/draw-panels.md). The [examples](examples.md) are
  complete plugins, tested in Rack, for the features of Rack's tutorial and API
  guide.
- **How does it work?** [The embedded runtime](runtime.md),
  [callbacks and the laboratory](explanation/callbacks.md),
  [security and trust](explanation/security.md), the
  [architecture](architecture.md) and the [decision records](decisions/index.md).
- **Exact answers?** The [reference](reference/index.md) covers every bound
  declaration, by Rack namespace.
- **Writing these docs?** See the [documentation conventions](contributing-docs.md).

```{toctree}
:caption: Start here
:maxdepth: 1
:hidden:

installation
quickstart
tutorials/oscillator
troubleshooting
examples
```

```{toctree}
:caption: How it works
:maxdepth: 1
:hidden:

explanation/execution
runtime
explanation/callbacks
explanation/security
```

```{toctree}
:caption: How-to guides
:maxdepth: 1
:hidden:

how-to/plugins
how-to/state
how-to/dsp
how-to/graphs
how-to/polyphony
how-to/menus
how-to/draw-panels
how-to/expanders
how-to/audio-midi
how-to/buffers
how-to/testing
how-to/profiling
how-to/packaging
how-to/migrate-from-cpp
```

```{toctree}
:caption: Design
:maxdepth: 1
:hidden:

scope
architecture
compatibility
coverage
performance
performance-results
licensing
documentation
decisions/index
```

```{toctree}
:caption: Reference
:maxdepth: 2
:hidden:

reference/index
```

```{toctree}
:caption: Contributing
:maxdepth: 1
:hidden:

contributing-bindings
contributing-docs
```
