# Installation

**There is nothing to install yet.** RackPy has no release, no plugin in the
VCV Library and no package on PyPI. Do not install anything that claims to be
RackPy.

## What exists today

| Platform | Status |
| --- | --- |
| Linux x86_64 | Builds from source, for Rack Free {{rack_version}}. Plugins written in Python load into Rack, add modules and process audio natively; the [examples](examples.md) run in Rack under test. Development builds work only on the computer that built them. |
| macOS (arm64, x86_64) | Not built. Planned in RP-40. |
| Windows x86_64 | Not built. Planned in RP-41. |
| Linux arm64, Windows arm64 | Not planned: VCV publishes no SDK for them. |

| Component | Version |
| --- | --- |
| RackPy | {{rackpy_version}} (unreleased) |
| Rack | Rack Free {{rack_version}}, commit {{rack_commit}} |
| Python inside the plugin | CPython {{python_version}}, bundled; the computer's own Python is never used |

## Building from source

The [quickstart](quickstart.md) builds RackPy and a plugin of your own, step
by step. In short, on Linux x86_64 with uv, CMake, Ninja, GCC and git:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
```

`/tmp/rackpy-bundle/RackPy` is RackPy itself, which `bin/rackpy-plugin build`
combines with your plugin project. The developer setup, the test categories and
the other commands are in [CONTRIBUTING.md](../CONTRIBUTING.md).

A development build needs the glibc of the machine that built it, and links to
RackPy's download cache for its Python runtime, so it is not for distribution
([ADR 005](decisions/005-build-and-ci.md), [packaging](how-to/packaging.md)).
