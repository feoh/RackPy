# RackPy

**Python for VCV Rack plugins: expressive at the surface, native where timing matters.**

:::{admonition} Development status
:class: important

RackPy is in **early development**. A Linux x86_64 development build loads
into Rack Free and binds part of the Rack API (values, engine components and
module construction; see the [coverage report](reference/coverage.md)), but
the plugin registers no modules yet and there is **no release** to install.
Pages describing unbuilt APIs are design proposals, labelled as such.
:::

RackPy will let you write Rack plugins in idiomatic Python, running inside
Rack, while the audio path stays native. The goal is the complete supported
public plugin API, proved symbol by symbol; see the
[coverage contract](coverage.md).

## Versions this documentation describes

| Component | Version |
| --- | --- |
| RackPy | {{rackpy_version}} (unreleased) |
| Rack SDK | {{rack_version}}, commit {{rack_commit}} |
| Bundled Python | CPython {{python_version}} ({{python_build}}) |
| Binding library | pybind11 {{pybind11_version}} |
| Platforms | Linux x86_64 only, from source; macOS and Windows not built yet |

## Where to go

- **Can I use it yet?** No. [Installation](installation.md) says what exists and
  how to build it from source.
- **How will it work?** Read the [architecture](architecture.md) and the
  [decision records](decisions/index.md). [The embedded runtime](runtime.md)
  explains what already works: start-up, threads, handles, shutdown and errors.
- **What will be covered?** The [SDK reference](reference/index.md) lists every
  declaration RackPy must bind, and which are bound so far.
- **Examples?** None are written yet; the [example plan](examples.md) lists the
  features each one must demonstrate.
- **Writing these docs?** See the [documentation conventions](contributing-docs.md).

Tutorials and troubleshooting pages will follow the
[documentation plan](documentation.md) once there is an API to teach. The
how-to guides cover [registering models](how-to/plugins.md),
[saving a module's state](how-to/state.md) and
[processing audio](how-to/dsp.md). [Callbacks and the laboratory](explanation/callbacks.md)
explains where Python may run, and the offline laboratory for audio callbacks.

```{toctree}
:caption: Start here
:maxdepth: 1
:hidden:

installation
examples
```

```{toctree}
:caption: How it works
:maxdepth: 1
:hidden:

runtime
explanation/callbacks
```

```{toctree}
:caption: How-to guides
:maxdepth: 1
:hidden:

how-to/plugins
how-to/state
how-to/dsp
how-to/graphs
how-to/draw-panels
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
