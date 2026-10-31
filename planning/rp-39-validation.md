# RP-39 validation: Linux artifacts on a clean machine

Task: `tk-rackpy-39-package-and-validate-linux-artifacts-o-2078d3`, branch `rp39`.
Build machine: Intel i9-14900K, Linux 7.3.0-4-cachyos x86_64, GCC 16.2.1 (host
builds), Python 3.14, Docker 29.9.2. Release builds use VCV's official Rack
toolchain (crosstool-ng, `x86_64-ubuntu16.04-linux-gnu`, build `4fd131870162`).
Rack Free 2.6.6 (`061ccf63c1758599396ac1bb10d47345d9d34076`). Nothing was
uploaded, published or attached to a release.

## What was delivered

- **`bin/rackpy-release build`** makes every Linux x86_64 artifact of a clean
  commit (it refuses a dirty checkout; `--allow-dirty` marks the manifest and
  must never leave the machine):

  | Artifact | Role |
  | --- | --- |
  | `RackPy<Example>-2.0.0-lin-x64.vcvplugin` (10) | Each example as a Rack plugin package |
  | `rackpy-runtime-<version>-lin-x64.tar.zst` | RackPy's runtime bundle, the input of `bin/rackpy-plugin build`; not a plugin itself |
  | `rackpy-0.0.0.dev0-py3-none-any.whl` | The developer package (typing, offline graphs); runs nothing in Rack |
  | `rackpy-<version>-source.tar.gz` | Corresponding source, `git archive` of the commit |
  | `manifest.json`, `SHA256SUMS` | Inputs, roles, checksums, ELF facts, exclusions |

  The runtime is extracted fresh from its pinned archive (not the build
  cache, which local runs touch), loses Tcl/Tk, `_dbm`, pip and `ensurepip`,
  and has its shared libraries stripped. The build fails on: an ELF dependency
  other than glibc's libraries, `libRack.so` or the bundle's own; a library
  search path other than Rack's `/tmp/Rack2` (added to every plugin by the
  SDK's `plugin.mk`) or `$ORIGIN`; a glibc symbol newer than 2.23; a file that
  names the build machine's home, checkout or caches; or a file no license rule
  covers.
- **Licenses in every artifact** (`licenses/`): `THIRD-PARTY-NOTICES.md`
  generated from the runtime build's own `PYTHON.json` (the same
  python-build-standalone build's full archive, now pinned in
  `packaging/inputs.json`), the texts of CPython and its linked libraries,
  pybind11's, `rack-sdk-headers.txt` (jansson, SIMDe, `sse_mathfun`, copied
  from the pinned SDK), and `FILES.json`: every file, its SHA-256, its
  components and the build's inputs (RackPy commit, Rack commit, SDK digest,
  CPython digest, toolchain). `docs/licensing.md` records the contents and the
  exclusions.
- **`bin/rackpy-release verify`** installs the artifacts on a clean container
  (`packaging/linux/ubuntu-20.04.Dockerfile`, glibc 2.31, and
  `ubuntu-16.04.Dockerfile`, glibc 2.23) with Rack Free's libraries, Xvfb and
  no Python interpreter, runs it with `--network none` as the caller's user,
  and runs `packaging/linux/verify-in-container.sh`, a fresh Rack user folder
  per run.
- **`bin/rackpy-release compare`** names each differing archive member of two
  builds.
- **`bin/rackpy-plugin pack`**: a plugin folder as a reproducible
  `.vcvplugin`, RackPy's `make dist`. `build` now drops the bundle's
  `FILES.json`, which describes the bundle, from user plugins.
- **CMake**: `-ffile-prefix-map` for the SDK and runtime paths, so `__FILE__`
  in header code no longer names the build machine's cache; example kernels
  get `SKIP_BUILD_RPATH` (they carried the SDK cache directory as an RPATH).
- **CI** `release-candidate` job (pushes to `main`, on demand): build, then
  verify on both images; keeps the manifest, checksums, logs and screenshots,
  never the binaries.
- **Docs**: `docs/installation/linux.md` (requirements, the files, verify,
  install, check, upgrade, uninstall, the load failures and their fixes),
  linked from `installation.md`; `how-to/packaging.md` gains "Make a package to
  install elsewhere"; `licensing.md`, ADR 002, ADR 005, `compatibility.md` and
  `CONTRIBUTING.md` updated. The last three manual samples (`presets-02`,
  `building-14`, `building-15`) have dispositions; none is left to a later task.

## Findings, fixed

| Finding | Fix |
| --- | --- |
| `rackpy_host.so` and the kernels named `/home/...` paths of the SDK and runtime caches (`__FILE__` in header code) | `-ffile-prefix-map` for both, in `CMakeLists.txt` |
| The example kernels' RPATH included the SDK cache directory | `SKIP_BUILD_RPATH`, as the loader has; they find `libRack.so` through Rack, as `plugin.so` does |
| Kernels were shipped unstripped, with the toolchain's build paths in their debug info | stripped in the release build |
| Stripping the runtime's patchelf-edited `bin/python3.14` breaks it (`symbol lookup error`) | only shared libraries are stripped; the stripped `libpython` was checked with every C extension module |
| Copying the runtime from the build cache would ship whatever local runs had written there | fresh extraction from the pinned archive |
| `FILES.json` first recorded the toolchain's cache path, missed because the path scan ran before the list was written | toolchain recorded by build ID and target; the scan runs last |
| Rack extracts a package over an existing folder without deleting it, so an upgrade can keep stale files | the install guide says to delete the folder first; the verifier tests exactly that |
| Rack's "modules not installed" dialog blocks a headless Rack without a dialog program | Rack's behaviour, not RackPy's; the uninstall check empties the patch first, and the guide describes the dialog |

## Evidence

Everything below is for commit `1ead7b4` unless it names `adc8245` (the
first commit of the tool, whose artifacts differ from `1ead7b4`'s only in the
two files that record the commit; see "Rebuilds"). The small records are in
[`rp-39-evidence/`](rp-39-evidence/); Rack's logs and the screenshots stay
outside git (`~/.cache/rackpy/evidence/rp39/`), because the screenshots show
Rack's artwork (see `docs/licensing.md`).

### The artifacts

`bin/rackpy-release build --dest /tmp/rp39-F` from the clean `1ead7b4`:
15 files ([`SHA256SUMS`](rp-39-evidence/SHA256SUMS),
[`manifest.json`](rp-39-evidence/manifest.json)). Each `.vcvplugin` is about
48 MB; the runtime bundle 48 MB; the wheel 146 KB; the source archive 2.2 MB.

| File (ELF) | Highest glibc symbol | Library search path |
| --- | --- | --- |
| `plugin.so` | 2.14 | `/tmp/Rack2` |
| `rackpy_host.so` (stripped, 46 MB) | 2.14 | `/tmp/Rack2:$ORIGIN/python/lib` |
| `kernels/{dsp,expanders,polyphony}.so` | 2.14 | `/tmp/Rack2` |
| `python/lib/libpython3.14.so.1.0` | 2.17 | none |
| `python/lib/libpython3.so` | 2.2.5 | none |
| `python/bin/python3.14` (unstripped, never run by Rack) | 2.17 | `$ORIGIN/../lib` |

Every `NEEDED` entry is glibc's (`libc`, `libm`, `libdl`, `libpthread`,
`librt`, `libutil`, `ld-linux-x86-64`), `libRack.so` or the bundle's
`libpython`. No file names the build machine (`machine_paths`, run on every
plugin folder after `FILES.json` is written). `licenses/FILES.json` covers all
3,824 files of the runtime bundle (3,829 in `RackPyDsp`).

### Clean machines

`bin/rackpy-release verify` on both images, `--network none` (the logs'
`network: 1 unknown` is the loopback interface, the only one), as uid 1000,
Rack Free 2.6.6 mounted read-only, a fresh user folder per run:

| Image | glibc | Python interpreter | Checks |
| --- | --- | --- | --- |
| Ubuntu 20.04.6 (`ubuntu@sha256:8feb4d8c…`) | 2.31 | none | 209 passed, 0 failed ([log](rp-39-evidence/verify-ubuntu-20.04.txt)) |
| Ubuntu 16.04.7 (`ubuntu@sha256:1f1a2d56…`) | 2.23 | none | 209 passed, 0 failed ([log](rp-39-evidence/verify-ubuntu-16.04.txt)) |

Per example plugin (10): every artifact matches `SHA256SUMS`; Rack extracts
the `.vcvplugin`; RackPy starts (CPython 3.14.9) and the plugin loads; all its
modules are created without a placeholder or a Python traceback and are in the
autosave; Rack quits cleanly and `Py_FinalizeEx` returns 0; a second run
reopens the autosave with every module; a windowed run under Xvfb opens the
module browser, filters it to the plugin, photographs the previews, and quits
with the interpreter finalized. Then: two RackPy plugins (the second is
refused by name), a loader of another ABI, a runtime missing `os.py`, an entry
package that raises (traceback logged, host init fails with code 3, Rack
carries on), an upgrade done as the guide says (remove the folder, copy the
package; the patch keeps its modules) and an uninstall.

The 10 browser screenshots (Ubuntu 20.04, `adc8245`, and spot-checked on
16.04) show each plugin's previews, with the browser's count matching the
models in `plugin.json`: 2, 1, 1, 1, 3, 1, 1, 2, 1, 1.

### Rebuilds

| Builds compared | Result |
| --- | --- |
| `adc8245` twice from the same checkout, different build folders | all 15 files bit-identical ([record](rp-39-evidence/rebuild-adc8245-same-checkout.json)) |
| `adc8245` from the checkout and from a fresh `git clone` in another folder with its own `uv sync` | all 15 files bit-identical ([record](rp-39-evidence/rebuild-adc8245-fresh-clone.json)) |
| `adc8245` against `1ead7b4` | each `.vcvplugin` differs only in `licenses/FILES.json` and `licenses/THIRD-PARTY-NOTICES.md`, which record the commit and version; the wheel is identical ([record](rp-39-evidence/compare-adc8245-1ead7b4.json)) |
| The source archive of `adc8245`, extracted without git, built with `bin/rackpy-dev build --toolchain rack-lin-x64` and bundled | `plugin.so`, `rackpy_host.so`, the three stripped kernels and `pythonpath/` byte-identical to the release's: the archive is the corresponding source |

The packaging guide's commands were run as written (release bundle extracted
with `tar --zstd`, `rackpy-plugin build`, `pack` twice): the packages were
identical, the bundle's `FILES.json` was left out, and `rackpy-dev inspect
--max-glibc 2.23` passed.

### Checks

- `prek run --all-files`, and strict mypy on `bin/rackpy-release` and
  `bin/rackpy-plugin`; `shellcheck -S warning` on the container script.
- Full `pytest`: 492 passed and 1 failed, then the failure fixed and its
  file re-run (15 passed). The failure was `test_conceptual_code_is_labelled_and_accepted`:
  the docs-build test's repository copy lacked `SECURITY.md`, which RP-38's
  security page links, so it was already failing on `main`. A first run had
  failed for lack of space in RAM-backed `/tmp` (the release builds' copies);
  it is not counted.
- `pytest -m dist`: 2 passed.
- `tests/integration/test_release.py` (6): reproducible packages without
  bytecode, `pack`'s naming and refusals, machine-path detection, `compare`,
  the license pin, and the file manifest's coverage.
- `tests/examples/test_dispositions.py`: 76 passed, none left to a later task.
- `sphinx-build -W --keep-going -n`: clean.

## Not covered, and why

- **Physical audio and MIDI devices**: none in the containers; the examples
  run with Rack's engine and no audio device, headless and in Xvfb.
- **Other distributions and GPU drivers**: Ubuntu 20.04 and 16.04 containers
  with Mesa llvmpipe, on this machine's kernel; not a VM, not real hardware
  GL. Fedora, Arch and others are untested.
- **Other build machines**: the rebuilds were all on this machine (same
  toolchain build, uv and CPython). CI's `release-candidate` job is the second
  machine; its `SHA256SUMS` can be compared with the ones above.
- **The module browser previews** were checked by eye (the screenshots); the
  automated checks are that no widget failed and no Python error was logged.
- **macOS and Windows**: RP-40 and RP-41.
- **Publication** needs owner approval (RP-44). The screenshot policy for
  `docs/_static/examples/` is still open; those images are left out of the
  source archive.
