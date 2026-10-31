# Package a plugin

:::{admonition} Pre-release: nothing here is for distribution yet
:class: warning

RackPy has no release. On Linux, `bin/rackpy-release build` makes release
candidates (RP-39) that install on other computers, and `bin/rackpy-plugin pack`
makes a `.vcvplugin` of your own plugin from one. macOS and Windows are RP-40
and RP-41. Until the owner approves a release (RP-44), do not distribute any of
them.
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

## Make a package to install elsewhere

A plugin that other Linux computers can load needs RackPy's release-candidate
runtime bundle, not a development bundle. Build the release candidate (it needs
VCV's toolchain, `bin/rack-toolchain build`, once), then combine and pack:

```bash
bin/rackpy-release build --dest /tmp/rackpy-release
tar --zstd -xf /tmp/rackpy-release/artifacts/rackpy-runtime-*-lin-x64.tar.zst -C /tmp/rackpy-release
bin/rackpy-plugin build MyPlugin --bundle /tmp/rackpy-release/RackPy --dest /tmp/my-plugin
bin/rackpy-plugin pack /tmp/my-plugin/MyPlugin --dest /tmp/my-plugin
```

`pack` writes `MyPlugin-<version>-lin-x64.vcvplugin`, the package format Rack
installs from (as `make dist` does for a C++ plugin). It is reproducible:
packing the same folder twice gives the same bytes, dated `SOURCE_DATE_EPOCH`.
Python bytecode caches are left out. It is installed by copying it into Rack's
`plugins-lin-x64` folder ([install on Linux](../installation/linux.md)).

RackPy builds its examples' kernel libraries with VCV's toolchain, so they
need no newer glibc than Rack's own plugins. A kernel library of your own,
built with the computer's compiler, needs that computer's glibc or newer;
`bin/rackpy-dev inspect <plugin folder> --max-glibc 2.23` says whether it is
older than the toolchain's.

The release bundle carries `LICENSE`, `NOTICE` and the notices of CPython and
its libraries in `licenses/`. Add your plugin's own license file (`LICENSE`,
`LICENSE.md` or `LICENSE.txt` in the project, which `build` copies as
`<slug>-LICENSE`, keeping its extension). The bundle's `licenses/FILES.json`
lists the bundle's own files, so `build` leaves it out of your plugin.

## Why a development build stays on its computer

- **The C library.** It links against the glibc of the computer that built it,
  so an older Linux cannot load it ([ADR 005](../decisions/005-build-and-ci.md)).
- **The Python runtime is a link.** `bin/rackpy-dev bundle` links `python/` to
  RackPy's download cache, so that bundling takes seconds. `--runtime copy`
  copies it instead, and strips its binaries; the result is still a
  development build.
- **Platform.** Linux x86_64 only, for Rack Free {{rack_version}}.

## Before anyone else runs it

A plugin that bundles Python needs what any plugin does, and more ([security and trust](../explanation/security.md#distributing-a-plugin-that-bundles-python)):

- **A licence compatible with GPL-3.0-or-later**, since it contains RackPy, and
  the notices of CPython and its libraries ([licensing](../licensing.md)).
- **One RackPy plugin per Rack.** Each carries its own Python, and RackPy
  refuses to start a second. Users with two RackPy plugins can run only one.
- **Checksums**, and signing where the platform expects it.
- **Rebuilds** when the bundled CPython has a security release.
