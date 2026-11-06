# Installation

**There is nothing to download yet.** RackPy has no release, no plugin in the
VCV Library and no package on PyPI. Do not install anything that claims to be
a RackPy release. On Linux, a Mac or a Windows PC you can build the release
candidates yourself and install them ([Linux](installation/linux.md),
[macOS](installation/macos.md), [Windows](installation/windows.md)).

## What exists today

| Platform | Status |
| --- | --- |
| Linux x86_64 | Builds from source, for Rack Free {{rack_version}}. Plugins written in Python load into Rack, add modules and process audio natively; the [examples](examples.md) run in Rack under test. Development builds work only on the computer that built them; [release candidates](installation/linux.md) (`bin/rackpy-release build`) install on any Linux with glibc 2.23 or newer, and are tested on a clean machine. |
| macOS (arm64, x86_64) | Builds from source on each architecture with Xcode's command line tools, for Rack Free {{rack_version}}; the native and host tests pass on both, headless. [Release candidates](installation/macos.md) (`bin/rackpy-release build` on a Mac, one package per architecture, macOS 11.0 or newer on Apple silicon, 10.15 or newer on Intel) are signed ad hoc, not with a Developer ID, and not notarized. Not yet checked: drawing in a window (the CI Macs have no OpenGL window), a Mac with SIP on, and a truly clean Mac. |
| Windows x86_64 | Builds from source with MSYS2's MinGW-w64 GCC, as Rack itself is built, for Rack Free {{rack_version}}; the native and host tests pass, headless. [Release candidates](installation/windows.md) (`bin/rackpy-release build` on Windows; Windows 10 or newer) are not Authenticode-signed. Not yet checked: drawing in a window (the CI runners have no OpenGL 2.0), a clean PC, and the sanitizer and audio-path guard programs, which are Linux-only. |
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
`bin/rackpy-release build` makes the release candidates instead, with VCV's
official toolchain: see [install on Linux](installation/linux.md).

```{toctree}
:hidden:

installation/linux
installation/macos
installation/windows
```
