# Installation

**There is nothing to install yet.** RackPy has no release, no plugin in the
VCV Library and no package on PyPI. Do not install anything that claims to be
RackPy.

## What exists today

| Platform | Status |
| --- | --- |
| Linux x86_64 | Builds from source. The plugin loads into Rack Free {{rack_version}} and starts CPython {{python_version}}; it has no modules. |
| macOS (arm64, x86_64) | Not built. Planned in RP-40. |
| Windows x86_64 | Not built. Planned in RP-41. |
| Linux arm64, Windows arm64 | Not planned: VCV publishes no SDK for them. |

## Building from source

The developer setup, the commands and the test categories are in
[CONTRIBUTING.md](../CONTRIBUTING.md). In short, on Linux x86_64 with uv,
CMake, Ninja and GCC:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy
```

`/tmp/rackpy/RackPy` is then a Rack plugin directory. Copying it into your own
Rack user folder is possible, but it does nothing useful yet, and the
development build needs the glibc of the machine it was built on
([ADR 005](decisions/005-build-and-ci.md)).
