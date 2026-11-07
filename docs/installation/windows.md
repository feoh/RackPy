# Install on Windows

:::{admonition} No Windows release is published
:class: warning

The only published release is Linux x86_64 for Rack Free 2.6.6. A successful
Windows release-candidate workflow uploads a downloadable CI artifact named
`windows-win-x64-release-candidate`; open the matching workflow run under
**Actions → CI** and download its artifact. Artifacts are retained for 30 days.
This is a test candidate, not a published or supported Windows release: it is
not on GitHub Releases, PyPI or the VCV Library. Do not redistribute or claim
Windows support based on it before the owner's hardware check.
:::

## What you need

- **Windows 10 or 11, 64-bit (x64).** The bundled CPython needs Windows 10.
  Windows on Arm is not a target: VCV publishes no SDK for it.
- **VCV Rack Free {{rack_version}}**, from the installer VCV publishes. Other
  Rack 2 versions are untested.
- **Nothing else.** Each plugin brings its own CPython {{python_version}}, and
  never uses a Python installed on the PC (python.org's, the Microsoft Store's
  or a conda one), anything on `PATH`, or the network.

[RP-41's validation record](../../planning/rp-41-validation.md) says what the
release candidates have actually been run on.

## What the files are

A release candidate is a folder of files, with `manifest.json` and `SHA256SUMS`:

| File | What it is | Install it? |
| --- | --- | --- |
| `RackPyOscillator-2.0.0-win-x64.vcvplugin` and the other `RackPy*.vcvplugin` | One of the [examples](../examples.md) as a Rack plugin package, the format Rack installs from the VCV Library | Yes, into Rack |
| `rackpy-runtime-<version>-win-x64.tar.zst` | RackPy itself, for `bin/rackpy-plugin build` to combine with your plugin project ([package a plugin](../how-to/packaging.md)) | No: it is not a plugin on its own |
| `rackpy-<version>-py3-none-any.whl` | The `rackpy` Python package, for editors, type checkers and offline graph tests | Into a virtual environment, if you write plugins |
| `rackpy-<version>-source.tar.gz` | The corresponding source code (GPL-3.0-or-later) | No |

**Install one RackPy plugin at a time**: each carries its own Python, and Rack
can run only one of them ([why](../explanation/security.md#what-rackpy-never-runs)).

## Verify the download

In PowerShell, in the folder of the release candidate:

```powershell
Get-Content SHA256SUMS | ForEach-Object {
    $sum, $name = $_ -split '  ', 2
    if ((Get-FileHash -Algorithm SHA256 $name).Hash -ne $sum.ToUpper()) { "MISMATCH $name" } else { "OK $name" }
}
```

Every line must say `OK`. `manifest.json` records the same checksums, the
commit the files were built from, the exact Rack SDK, CPython and compiler
they were built with, and for every DLL the DLLs it imports and the functions
it exports.

## What is inside, and what it loads

A RackPy plugin on Windows has three kinds of code:

- `plugin.dll`, `rackpy_host.dll` and any `kernels\*.dll`, built with MinGW-w64
  GCC on the msvcrt C runtime, exactly as Rack itself is: they call Rack's C++
  API, which only the same compiler family can use.
- `python\python314.dll` and the rest of `python\`: python-build-standalone's
  build of CPython, made with Microsoft's compiler and the Universal C
  Runtime, with Microsoft's `vcruntime140.dll` beside it. RackPy uses it only
  through CPython's C API.
- The plugin's Python code in `pythonpath\`.

RackPy loads `python314.dll` by its full path, and Windows then searches only
the plugin's `python\` folder and System32 for what that DLL needs: never
`PATH`, the current folder or Rack's folder. If another plugin has already
loaded a CPython DLL, RackPy refuses to start rather than share it.
`sys.path` holds only the plugin's runtime and its `pythonpath\`.

`vcruntime140.dll` is Microsoft Distributable Code. It is redistributed as
python.org's Windows builds redistribute it, under the conditions in
`python\LICENSE.txt` ("Additional Conditions for this Windows binary build");
`licenses\FILES.json` lists it as its own component.

## Signing and SmartScreen

The DLLs are **not Authenticode-signed**: RackPy has no code-signing
certificate, and signing is a separate decision for the owner. Rack Free
itself loads unsigned plugin DLLs, as it loads every plugin from the VCV
Library. SmartScreen warns about programs you open, and RackPy has none:
Rack loads the DLLs.

A file downloaded with a browser carries the Mark of the Web (an "Internet
zone" note Windows keeps beside the file). It does not stop RackPy: on the CI
runners, Rack extracted a marked `.vcvplugin` into files that carry no mark,
and a plugin folder whose every file was marked loaded and started normally.
Neither needs **Unblock** in the file's properties.

## Install

1. Quit Rack.
2. Copy the `.vcvplugin` file into the `plugins-win-x64` folder of Rack's user
   folder, `%LOCALAPPDATA%\Rack2\`:

   ```powershell
   Copy-Item RackPyOscillator-2.0.0-win-x64.vcvplugin "$env:LOCALAPPDATA\Rack2\plugins-win-x64\"
   ```

3. Start Rack. It extracts the package into a `RackPyOscillator` folder next
   to it and deletes the package file.

Rack's own **Help > Open user folder** menu item opens the user folder in
Explorer. Copy the `.vcvplugin` file itself; do not unpack it first.

Keep the user folder's path short. A user folder whose path makes the
plugin's files longer than 260 characters does not work, even with Windows'
long-path setting turned on: on the CI runners Rack itself did not get as far
as writing its log. Rack's default folder is far below that. A folder whose
name is not ASCII (for example `Ünïcødé 音楽`) works.

## Check that it works

1. Right-click an empty part of the rack, or press Enter, to open the module
   browser, and type the plugin's name (for example `RackPy Oscillator`). Its
   modules appear, each drawn as a preview.
2. Add a module. It should work as its [example](../examples.md) describes.
3. Rack's log, `log.txt` in the user folder, should have these lines from
   RackPy, in this order:

   ```text
   RackPy interpreter started: Python 3.14.9 ... [MSC v.1944 64 bit (AMD64)]
   RackPy host rackpy_host.dll ready
   Loaded plugin RackPyOscillator 2.0.0
   ```

   and, when Rack quits, `RackPy interpreter finalized: Py_FinalizeEx returned 0`.

## Upgrade

Rack extracts a package over the folder of the same name without deleting it
first, so files that a newer version no longer has would stay. Remove the old
folder before copying the new package:

1. Quit Rack.
2. Delete `%LOCALAPPDATA%\Rack2\plugins-win-x64\RackPyOscillator\`.
3. Install the new `.vcvplugin` as above.

Patches keep working across upgrades: a module's saved state is in the patch,
not in the plugin folder.

## Uninstall

1. Quit Rack.
2. Delete the plugin's folder, for example
   `%LOCALAPPDATA%\Rack2\plugins-win-x64\RackPyOscillator\`.

A patch that used the plugin's modules still opens: Rack lists the modules it
cannot find and offers to look for them in the VCV Library, and leaves them
out. Saving the patch then drops them and their settings, so keep a copy of a
patch you may want to use with the plugin again.

## When it does not load

Rack keeps going when a plugin fails, and says why in `log.txt`. Search it for
`RackPy` or the plugin's name:

| Log says | Cause | Fix |
| --- | --- | --- |
| `RackPy refused to start: cannot load .../python/python314.dll: %1 is not a valid Win32 application. (Windows error 193)` | `python314.dll` is damaged, or is not a 64-bit Windows DLL | Delete the folder and install the package again |
| `RackPy refused to start: cannot load .../python/python314.dll: Access is denied. (Windows error 5)` | Your account cannot read the plugin's files (permissions changed, or another account installed them) | Give your account read access to the plugin folder, or install it again yourself |
| `RackPy refused to start: cannot load .../python/python314.dll:`, then another reason and Windows error number | A DLL the runtime needs is missing from the plugin's `python\` folder (for example `vcruntime140.dll`), from an interrupted copy or a cleanup tool | Delete the folder and install the package again |
| `RackPy refused to start: a CPython runtime is already loaded in this process (...)` | Two RackPy plugins, or another plugin that embeds Python | Keep one of them: move the other's folder out of `plugins-win-x64` |
| `RackPy refused to start: .../rackpy-loader.json: "abi" must be 2, the host ABI this loader was built for` | The plugin's `rackpy-loader.json` comes from another RackPy version than its `plugin.dll`, usually from a package extracted over an older folder | Delete the folder and install the package again |
| `RackPy refused to start: bundled Python runtime missing or incomplete (expected ...)` | Files of the plugin's `python\` folder are missing | Delete the folder and install the package again |
| `RackPy host initialization failed (...)`, after a Python traceback | The plugin's own Python code failed while Rack loaded it | Report it to the plugin's author, with the traceback |
| `Plugin package ... failed to extract` | A damaged download | Check it as above, and download it again |

Each `RackPy refused to start` line is followed by Rack's own `Could not load
plugin ...` line, naming a module of the plugin: that is the consequence, not
a second problem. The first five rows are text from Windows runs of
`verify.ps1`. A missing `vcruntime140.dll` could not be shown there, because
the runners have one in System32, so its third row gives no exact text; the
last two rows are RackPy's and Rack's own messages, not specific to Windows.

The [troubleshooting](../troubleshooting.md) page covers what happens after a
plugin loads.

### For plugin authors: Rack's console handles

On Windows, Rack (msvcrt) and the bundled CPython (the Universal C Runtime)
each have their own C runtime, with their own file descriptors, over the same
Windows console handles. Python code that closes or replaces file descriptors
0, 1 or 2 (`os.close(0)`, `os.dup2(..., 0)`, or a library that captures output
that way, such as pytest's default `--capture=fd`) closes the handle Rack
itself uses: headless Rack then reads the end of its input and quits. Redirect
`sys.stdout` and `sys.stderr` instead, which stays inside Python.

## How the release candidates are checked

`bin/rackpy-release build` checks every DLL of every package as it builds
them: a 64-bit x64 file; nothing imported but DLLs the plugin bundles, Rack's
own (`libRack.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`,
`libstdc++-6.dll`) and Windows'; and no Universal C Runtime in RackPy's own
DLLs, which must use msvcrt as Rack does. `bin/rackpy-release verify` then
installs each package into a fresh Rack user folder and runs Rack with
Windows PowerShell and `PATH` limited to Windows' own folders. For each plugin
it checks that Rack extracts and loads it, that all its modules run, are saved
and come back when the patch is reopened, that the only CPython Rack loaded is
the plugin's own and that every DLL Rack loaded is Rack's, the plugin's or
Windows', and that Rack quits with the interpreter finalized. It also checks
each failure in the table above; that decoy DLLs in Rack's current folder and
on `PATH` are never loaded; a user folder whose path is not ASCII; an upgrade
and an uninstall; and records what happens with a path longer than 260
characters and with files that carry the Mark of the Web. While Rack runs, it
lists the DLLs Rack has loaded from Windows' own record of them.

It runs on GitHub's `windows-2025` runners. Those are not clean PCs, and they
have no GPU, so the windowed checks (Rack drawing each module) are reported as
open there rather than passed.
[RP-41's validation record](../../planning/rp-41-validation.md) has the results
and the open gates. A PC's sound card and MIDI devices are not part of it.
