# Install on macOS

:::{admonition} Release candidates only: nothing is published
:class: warning

RackPy has no release. The files this page describes are release candidates
that `bin/rackpy-release build` makes from a commit on a Mac, and the CI
macOS release-candidate jobs make from `main`. They are not on GitHub
Releases, PyPI or the VCV Library, and must not be redistributed until the
owner approves a release (RP-44). Do not install a file that claims to be a
RackPy release from anywhere else.
:::

## What you need

- **A Mac with Apple silicon (arm64) running macOS 11 or newer, or an Intel
  Mac (x86_64) running macOS 10.15 or newer.** Those are the oldest versions
  the bundled CPython supports on each architecture; Rack itself supports
  older Intel Macs, but RackPy cannot. Each architecture has its own packages:
  `mac-arm64` for Apple silicon and `mac-x64` for Intel. A package for both
  (a universal binary) is not made.
- **VCV Rack Free {{rack_version}}**. Other Rack 2 versions are untested.
- **Nothing else.** Each plugin brings its own CPython {{python_version}}, and
  never uses a Python installed on the Mac (Apple's, python.org's or
  Homebrew's), or the network.

[RP-40's validation record](../../planning/rp-40-validation.md) says which
architectures and macOS versions the release candidates have actually been
run on. An architecture without native results there is not supported.

## What the files are

A release candidate is a folder of files, with `manifest.json` and `SHA256SUMS`:

| File | What it is | Install it? |
| --- | --- | --- |
| `RackPyOscillator-2.0.0-mac-arm64.vcvplugin` and the other `RackPy*.vcvplugin` | One of the [examples](../examples.md) as a Rack plugin package, the format Rack installs from the VCV Library | Yes, into Rack |
| `rackpy-runtime-<version>-mac-arm64.tar.zst` | RackPy itself, for `bin/rackpy-plugin build` to combine with your plugin project ([package a plugin](../how-to/packaging.md)) | No: it is not a plugin on its own |
| `rackpy-<version>-py3-none-any.whl` | The `rackpy` Python package, for editors, type checkers and offline graph tests | Into a virtual environment, if you write plugins |
| `rackpy-<version>-source.tar.gz` | The corresponding source code (GPL-3.0-or-later) | No |

On an Intel Mac the names say `mac-x64` instead. **Install one RackPy plugin
at a time**: each carries its own Python, and Rack can run only one of them
([why](../explanation/security.md#what-rackpy-never-runs)).

## Verify the download

In Terminal, in the folder of the release candidate:

```bash
shasum -a 256 -c SHA256SUMS
```

Every line must say `OK`. `manifest.json` records the same checksums, the
commit the files were built from, the exact Rack SDK, CPython, Xcode compiler
and macOS SDK they were built with, and for every binary its architecture,
the libraries it loads, its oldest macOS and its code signature.

## Signing and Gatekeeper

The release candidates' binaries are **signed ad hoc**, as `make dist` signs
Rack plugins (`codesign -f -s -`): every file has a valid signature, which
Apple silicon requires before it runs any code, but no developer identity.
They are **not signed with a Developer ID, and not notarized by Apple**: RackPy
has no Apple signing credentials. Rack loads such plugins because Rack Free is
built to accept libraries signed by anyone (its
`com.apple.security.cs.disable-library-validation` entitlement), as it loads
every plugin from the VCV Library.

What Gatekeeper does depends on how the files arrived:

- **A downloaded `.vcvplugin` package** is marked as quarantined by the
  browser, but Rack extracts it with its own code, and the extracted files are
  not quarantined. Copy the package itself into the plugins folder (below);
  this is checked on both architectures.
- **An already unpacked plugin folder** that came from a browser download (an
  unzipped folder, for example) has every file quarantined. macOS may then
  check, or refuse, each library as Rack loads it. Clear the attribute before
  starting Rack:

  ```bash
  xattr -dr com.apple.quarantine ~/Library/Application\ Support/Rack2/plugins-mac-arm64/RackPyOscillator
  ```

The CI Macs RackPy is tested on had Gatekeeper on (`spctl --status`:
assessments enabled) but System Integrity Protection **off**. On them, Rack
loaded such a quarantined folder, and even a `plugin.dylib` whose signature had
been removed, without complaint. Apple silicon normally refuses to run unsigned
code at all, so those machines evidently enforce code signing less strictly
than a Mac as shipped, and what a personal Mac with SIP on does with a
quarantined folder is not established. It is an open gate in
[RP-40's validation record](../../planning/rp-40-validation.md); until it is
closed, clear the attribute as above, and never remove the ad-hoc signature.

## Install

1. Quit Rack.
2. Copy the `.vcvplugin` file into the `plugins-mac-arm64` folder (on an Intel
   Mac, `plugins-mac-x64`) of Rack's user folder,
   `~/Library/Application Support/Rack2/`:

   ```bash
   cp RackPyOscillator-2.0.0-mac-arm64.vcvplugin ~/Library/Application\ Support/Rack2/plugins-mac-arm64/
   ```

3. Start Rack. It extracts the package into a `RackPyOscillator` folder next
   to it and deletes the package file.

Rack's own **Help > Open user folder** menu item opens the user folder in the
Finder. Copy the `.vcvplugin` file itself; do not unpack it first.

If you run Rack with **Open using Rosetta** on Apple silicon, it is the Intel
Rack: it loads the `mac-x64` packages from `plugins-mac-x64`, and the Apple
silicon ones do not load.

## Check that it works

1. Right-click an empty part of the rack, or press Enter, to open the module
   browser, and type the plugin's name (for example `RackPy Oscillator`). Its
   modules appear, each drawn as a preview.
2. Add a module. It should work as its [example](../examples.md) describes.
3. Rack's log, `log.txt` in the user folder, should have these lines from
   RackPy, in this order:

   ```text
   RackPy interpreter started: Python 3.14.9 ...
   RackPy host rackpy_host.so ready
   Loaded plugin RackPyOscillator 2.0.0
   ```

   and, when Rack quits, `RackPy interpreter finalized: Py_FinalizeEx returned 0`.

## Upgrade

Rack extracts a package over the folder of the same name without deleting it
first, so files that a newer version no longer has would stay. Remove the old
folder before copying the new package:

1. Quit Rack.
2. Delete `~/Library/Application Support/Rack2/plugins-mac-arm64/RackPyOscillator/`.
3. Install the new `.vcvplugin` as above.

Patches keep working across upgrades: a module's saved state is in the patch,
not in the plugin folder.

## Uninstall

1. Quit Rack.
2. Delete the plugin's folder, for example
   `~/Library/Application Support/Rack2/plugins-mac-arm64/RackPyOscillator/`.

A patch that used the plugin's modules still opens: Rack lists the modules it
cannot find and offers to look for them in the VCV Library, and leaves them
out. Saving the patch then drops them and their settings, so keep a copy of a
patch you may want to use with the plugin again.

## When it does not load

Rack keeps going when a plugin fails, and says why in `log.txt`. Search it for
`RackPy` or the plugin's name:

| Log says | Cause | Fix |
| --- | --- | --- |
| `Could not load plugin ... incompatible architecture (have 'x86_64', need 'arm64e' or 'arm64')` | The package for the other kind of Mac: `mac-x64` on Apple silicon (or `mac-arm64` on Intel) | Delete the folder and install the package whose name matches your Mac |
| `Could not load plugin ...` with `code signature` or `not valid for use in process` | The plugin's `plugin.dylib` was changed after it was signed, or its signature removed | Delete the folder and install the package again, or sign it again ad hoc: `codesign --force --sign - .../plugin.dylib` |
| `RackPy refused to start: a CPython runtime is already loaded in this process` | Two RackPy plugins, or another plugin that embeds Python | Keep one of them: move the other's folder out of `plugins-mac-arm64` |
| `RackPy refused to start: "abi" must be ...` | The plugin's `rackpy-loader.json` comes from another RackPy version than its `plugin.dylib`, usually from a package extracted over an older folder | Delete the folder and install the package again |
| `RackPy refused to start: bundled Python runtime missing or incomplete` | Files of the plugin's `python/` folder are missing, from an interrupted copy or a cleanup tool | Delete the folder and install the package again |
| `RackPy host initialization failed (3)`, after a Python traceback | The plugin's own Python code failed while Rack loaded it | Report it to the plugin's author, with the traceback |
| `Plugin package ... failed to extract` | A damaged download | Check it with `shasum -a 256 -c SHA256SUMS`, and download it again |

The [troubleshooting](../troubleshooting.md) page covers what happens after a
plugin loads.

## How the release candidates are checked

`bin/rackpy-release build` checks every binary of every package as it builds
them: one architecture, this Mac's; nothing loaded but macOS's own libraries,
Rack's `libRack` (by the loader only) and the plugin's own Python; no other
library search path; the oldest macOS claimed above; and a signature that
verifies. `bin/rackpy-release verify` then installs each package into a fresh
Rack user folder and runs Rack with an empty environment. For each plugin it
checks that Rack extracts and loads it, that all its modules run, are saved
and come back when the patch is reopened, that the only Python Rack loaded is
the plugin's own (nothing from Homebrew, `/usr/local` or python.org's
framework), and that Rack quits with the interpreter finalized. It also checks
each failure in the table above, an upgrade and an uninstall, and records what
macOS does with an unsigned loader and with quarantined files.

It runs on GitHub's macOS runners, one per architecture. Those are not clean
Macs, and they give Rack no OpenGL window, so the windowed checks (Rack drawing
each module) are reported as open there rather than passed.
[RP-40's validation record](../../planning/rp-40-validation.md) has the results
and the open gates. A Mac's sound card and MIDI devices are not part of it.
