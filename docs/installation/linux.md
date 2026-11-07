# Install on Linux

:::{admonition} Linux x86_64 only
:class: warning

RackPy `1.0.0` is published for VCV Rack Free 2.6.6 on Linux x86_64. Download
the release assets from [GitHub Releases](https://github.com/feoh/RackPy/releases/tag/v1.0.0).
This is not a complete API-coverage, performance, real-time deadline or broad
platform-support claim. There is no PyPI upload or VCV Library submission.
:::

## What you need

- **Linux x86_64** with glibc 2.23 (Ubuntu 16.04's) or newer. The release
  was built with VCV's own Rack plugin toolchain, the one Rack's own plugins use,
  and its binaries are checked against the glibc 2.23 floor. The exact v1.0.0
  artifacts passed install/run verification in clean Ubuntu 20.04 and 16.04
  containers; see the [candidate audit](../../release-evidence/candidate-audit.md).
- **VCV Rack Free {{rack_version}}**. Other Rack 2 versions are untested.
- **Nothing else at run time.** Each plugin brings its own CPython {{python_version}},
  and never uses a Python installed on the computer or the network.
- **To build and verify:** Git, uv, Docker, network access for the pinned inputs,
  and VCV Rack Free 2.6.6. Docker is used to build VCV's pinned Linux toolchain
  and run clean-machine verification; it is not required to install the finished
  plugin in Rack.

## Build and verify from source

The selected Python package version is `1.0.0`. The release builder adds
`+g<commit>` to identify the source commit; the example `.vcvplugin` names and
Rack log continue to use `2.0.0`, Rack's plugin version field. Build from a
clean checkout of the desired release commit (the release tag identifies its
source):

```bash
git clone https://github.com/feoh/RackPy.git RackPy-preview
cd RackPy-preview
git fetch origin --tags
git checkout v1.0.0
uv sync --frozen
bin/rack-toolchain build  # one-time toolchain build; may take about an hour
RELEASE_DIR=/tmp/rackpy-release
bin/rackpy-release build --dest "$RELEASE_DIR"
(cd "$RELEASE_DIR/artifacts" && sha256sum -c SHA256SUMS)
RACK_FREE=/path/to/unzipped/Rack2Free
bin/rackpy-release verify "$PREVIEW_DIR/artifacts" \
  --rack "$RACK_FREE" --logs "$RELEASE_DIR/verify-ubuntu-20.04"
```

`build` empties the destination first, so choose a disposable directory outside
the checkout. It writes the artifacts under `artifacts/`, plus their manifest and
checksums. `verify` runs the packages in a clean Ubuntu 20.04 container with no
system Python and no network, exercises all ten example plugins, and saves its
logs and screenshots in the `--logs` directory. Do not use `--allow-dirty` when producing
release artifacts. A successful container test does not prove physical audio-device
deadlines or audio-interface behavior; the optional host install below is for
manual Rack use. The exact source commit and verification results for v1.0.0 are recorded in
the [candidate audit](../../release-evidence/candidate-audit.md).

## What the files are

A GitHub release download contains these files, with `manifest.json` and `SHA256SUMS`:

| File | What it is | Install it? |
| --- | --- | --- |
| `RackPyOscillator-2.0.0-lin-x64.vcvplugin` and the other `RackPy*.vcvplugin` | One of the [examples](../examples.md) as a Rack plugin package, the format Rack installs from the VCV Library | Yes, into Rack |
| `rackpy-runtime-<version>-lin-x64.tar.zst` | RackPy itself, for `bin/rackpy-plugin build` to combine with your plugin project ([package a plugin](../how-to/packaging.md)) | No: it is not a plugin on its own |
| `rackpy-<version>-py3-none-any.whl` | The `rackpy` Python package, for editors, type checkers and offline graph tests | Into a virtual environment, if you write plugins |
| `rackpy-<version>-source.tar.gz` | The corresponding source code (GPL-3.0-or-later) | No |

Each plugin package is about 48 MB (173 MB once Rack extracts it), most of it
the Python runtime. **Install one RackPy plugin at a time**: each carries its
own Python, and Rack can run only one of them
([why](../explanation/security.md#what-rackpy-never-runs)).

## Verify the download

In the folder containing the downloaded release assets:

```bash
sha256sum -c SHA256SUMS
```

Every line must say `OK`. `manifest.json` records the same checksums, the
commit the files were built from, and the exact Rack SDK, CPython and toolchain
they were built with.

## Install

1. Quit Rack.
2. Copy the `.vcvplugin` file into the `plugins-lin-x64` folder of Rack's user
   folder, `~/.local/share/Rack2/` (or `$XDG_DATA_HOME/Rack2/` if you set
   `XDG_DATA_HOME`):

   ```bash
   cp RackPyOscillator-2.0.0-lin-x64.vcvplugin ~/.local/share/Rack2/plugins-lin-x64/
   ```

3. Start Rack. It extracts the package into a `RackPyOscillator` folder next
   to it and deletes the package file.

Rack's own **Help > Open user folder** menu item opens the user folder.

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
2. Delete `~/.local/share/Rack2/plugins-lin-x64/RackPyOscillator/`.
3. Install the new `.vcvplugin` as above.

Patches keep working across upgrades: a module's saved state is in the patch,
not in the plugin folder.

## Uninstall

1. Quit Rack.
2. Delete the plugin's folder, for example
   `~/.local/share/Rack2/plugins-lin-x64/RackPyOscillator/`.

A patch that used the plugin's modules still opens: Rack lists the modules it
cannot find and offers to look for them in the VCV Library, and leaves them
out. Saving the patch then drops them and their settings, so keep a copy of a
patch you may want to use with the plugin again.

## When it does not load

Rack keeps going when a plugin fails, and says why in `log.txt`. Search it for
`RackPy`:

| Log says | Cause | Fix |
| --- | --- | --- |
| `RackPy refused to start: a CPython runtime is already loaded in this process` | Two RackPy plugins, or another plugin that embeds Python | Keep one of them: move the other's folder out of `plugins-lin-x64` |
| `RackPy refused to start: "abi" must be ...` | The plugin's `rackpy-loader.json` comes from another RackPy version than its `plugin.so`, usually from a package extracted over an older folder | Delete the folder and install the package again |
| `RackPy refused to start: bundled Python runtime missing or incomplete` | Files of the plugin's `python/` folder are missing, from an interrupted copy or a cleanup tool | Delete the folder and install the package again |
| `RackPy host initialization failed (3)`, after a Python traceback | The plugin's own Python code failed while Rack loaded it | Report it to the plugin's author, with the traceback |
| `Could not load plugin ... GLIBC_2.xx not found` | A development build, made on a newer Linux, not a release candidate | Install the release candidate, or build on that computer |
| `Plugin package ... failed to extract` | A damaged download | Check it with `sha256sum -c SHA256SUMS`, and download it again |

The [troubleshooting](../troubleshooting.md) page covers what happens after a
plugin loads.

## How the release candidates are checked

`bin/rackpy-release verify` installs every plugin package of a release
candidate on a clean Ubuntu 20.04 container, and with `--image ubuntu-16.04`
on Ubuntu 16.04 (glibc 2.23, the oldest claimed): no RackPy checkout, no
Python on the system and no network, with a fresh Rack user folder for each
run. For each
plugin it checks that Rack extracts and loads it, that all of its modules run,
are saved and come back when the patch is reopened, that the module browser
draws their previews, and that Rack quits with the interpreter finalized. It
also checks each failure in the table above. Its logs and screenshots are kept
with the build; [RP-39's validation record](../../planning/rp-39-validation.md)
has the results. A computer's sound card and MIDI devices are not part of it.
