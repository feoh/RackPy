# Installation

RackPy **1.0.0** is available from [GitHub Releases](https://github.com/feoh/RackPy/releases)
for Linux x86_64 and Rack Free 2.6.6. Coverage remains partial; no PyPI package
or VCV Library listing exists. macOS and Windows artifacts are CI-tested
candidates only, not supported releases. See [Linux installation](installation/linux.md),
[macOS](installation/macos.md), and [Windows](installation/windows.md).

## What exists today

| Platform | Status |
| --- | --- |
| Linux x86_64 | **Published: RackPy 1.0.0** for Rack Free {{rack_version}}. The GitHub artifacts install on glibc 2.23 or newer and passed clean Ubuntu 16.04/20.04 verification; see [scope and evidence](installation/linux.md). Development builds remain local to their build machine. |
| macOS (arm64, x86_64) | Builds from source on each architecture with Xcode's command line tools, for Rack Free {{rack_version}}; the native and host tests pass on both, headless. [Release candidates](installation/macos.md) (`bin/rackpy-release build` on a Mac, one package per architecture, macOS 11.0 or newer on Apple silicon, 10.15 or newer on Intel) are signed ad hoc, not with a Developer ID, and not notarized. Not yet checked: drawing in a window (the CI Macs have no OpenGL window), a Mac with SIP on, and a truly clean Mac. |
| Windows x86_64 | Builds from source with MSYS2's MinGW-w64 GCC, as Rack itself is built, for Rack Free {{rack_version}}; the native and host tests pass, headless. [Release candidates](installation/windows.md) (`bin/rackpy-release build` on Windows; Windows 10 or newer) are not Authenticode-signed. Not yet checked: drawing in a window (the CI runners have no OpenGL 2.0), a clean PC, and the sanitizer and audio-path guard programs, which are Linux-only. |
| Linux arm64, Windows arm64 | Not planned: VCV publishes no SDK for them. |

| Component | Version |
| --- | --- |
| RackPy | {{rackpy_version}} (Linux x86_64 GitHub release) |
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
