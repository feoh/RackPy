# Package a plugin

:::{admonition} Pre-release: nothing here is for distribution yet
:class: warning

RackPy has no release, and the plugins its development builds make work only on
the computer that built them. Packaging for distribution (a `.vcvplugin` for
each platform, built on a known system, with the bundled Python's licences) is
RackPy's release work: RP-39 for Linux, RP-40 for macOS, RP-41 for Windows.
This page describes what exists now.
:::

## What a RackPy plugin contains

`bin/rackpy-plugin build` makes a Rack plugin folder from two parts:

| Part | From | Holds |
| --- | --- | --- |
| Your project | `MyPlugin/` | `plugin.json`, `pythonpath/` (your Python), `res/` (panels), `presets/`, and the C++ kernel libraries its `rackpy-plugin.json` lists |
| RackPy's bundle | `bin/rackpy-dev bundle` | `plugin.so` (the loader Rack opens), `rackpy_host.so`, the `rackpy` package, and `python/`: CPython {{python_version}} and its standard library |

```bash
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build MyPlugin --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

A plugin with C++ kernels also passes `--kernels`, the folder where they were
built ([process audio](dsp.md#write-a-kernel-in-c)).

## Why a development build stays on its computer

- **The C library.** It links against the glibc of the computer that built it,
  so an older Linux cannot load it ([ADR 005](../decisions/005-build-and-ci.md)).
- **The Python runtime is a link.** `bin/rackpy-dev bundle` links `python/` to
  RackPy's download cache, so that bundling takes seconds. `--runtime copy`
  copies it instead, and strips its binaries; the result is still a
  development build.
- **Platform.** Linux x86_64 only, for Rack Free {{rack_version}}.

## Before anyone else runs it

When releases exist, a plugin that bundles Python will need what any plugin
does, and more ([security and trust](../explanation/security.md#distributing-a-plugin-that-bundles-python)):

- **A licence compatible with GPL-3.0-or-later**, since it contains RackPy, and
  the notices of CPython and its libraries ([licensing](../licensing.md)).
- **One RackPy plugin per Rack.** Each carries its own Python, and RackPy
  refuses to start a second. Users with two RackPy plugins can run only one.
- **Checksums**, and signing where the platform expects it.
- **Rebuilds** when the bundled CPython has a security release.
