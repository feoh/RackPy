# RP-41 validation: Windows artifacts with explicit loader and runtime rules

Task: `tk-rackpy-41-package-and-validate-windows-artifacts-a352b0`, branch `rp41`.
Everything below ran natively on GitHub-hosted `windows-2025` runners
(Windows Server 2025 Datacenter 10.0.26100, x64), building with MSYS2's
MINGW64 GCC 16.2.0. Rack Free 2.6.6 (`061ccf63c1758599396ac1bb10d47345d9d34076`),
unpacked from VCV's `RackFree-2.6.6-win-x64.exe` installer. The cross build on
Linux with the official toolchain (`--toolchain rack-win-x64`, GCC 15.1.0,
mingw-w64 v10) and runs under Wine were used only to find problems before a
CI run: neither is Windows evidence. Nothing was uploaded, published or
Authenticode-signed.

## What was delivered

- **The ABI rules** (`docs/compatibility.md`, finding 2): RackPy's DLLs
  (`plugin.dll`, `rackpy_host.dll`, kernels) are built with MinGW-w64 GCC on
  msvcrt, as Rack is, `-static-libstdc++`. The bundled CPython 3.14.9 is
  python-build-standalone's MSVC build on the Universal C Runtime, used only
  through its C API: no `FILE*`, heap block or `errno` crosses between the two
  C runtimes. Python 3.14 needs Windows 10, the floor.
- **The loader rules** (`native/runtime/dynlib.cpp`, `rackpy::dynlib`):
  `python314.dll` is loaded by its full path with `LoadLibraryExW` and
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`, so its
  dependencies come only from its own folder and System32, never `PATH`, the
  current folder or Rack's folder; the module is pinned; Windows' error dialogs
  are off around the load, so a bad image is logged (error 193) instead of
  hanging Rack. Containment checks resolve paths with
  `GetFinalPathNameByHandleW`. `sys.path` is set explicitly to the runtime's
  `python314.zip`, `DLLs` and `Lib`, then the plugin's `pythonpath`; a second
  CPython in the process is refused by name, found in the process's module
  list (`EnumProcessModules`, full UTF-16 paths of any length).
- **The build** (`CMakeLists.txt`, `cmake/RackSDK.cmake`,
  `cmake/toolchains/rack-win-x64.cmake`): a `win-x64` platform against the
  SDK's `libRack.dll.a`, with `-D_USE_MATH_DEFINES -municode`;
  `bin/rack-toolchain --target win-x64`; `bin/rack-sdk host win-x64` unpacks
  Rack's NSIS installer with 7-Zip; `packaging/inputs.json` pins the Windows
  CPython.
- **`bin/rackpy-release build` on Windows** strips RackPy's MinGW DLLs (not
  Microsoft's), drops the CPython `*.pdb` files, Tk, IDLE, pip and ensurepip,
  and fails on any PE file that is not x64, imports anything but the plugin's
  own DLLs, Rack's (`libRack.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
  `libwinpthread-1.dll`) and Windows', or (for RackPy's own DLLs) the
  Universal C Runtime, or needs a newer Windows than 10. The manifest records
  each DLL's imports and exports. `vcruntime140*.dll` is its own
  `msvc-runtime` licence component.
- **`bin/rackpy-release verify` on Windows** runs `packaging/windows/verify.ps1`
  in Windows PowerShell 5.1 with `PATH` limited to Windows' folders: fresh user
  folders, every example headless twice (save, reopen), the DLLs Rack loaded
  (PSAPI module lists taken while it runs), the error cases (another loader
  ABI, an incomplete runtime, a damaged and an unreadable `python314.dll`, two
  RackPy plugins, a missing `vcruntime140.dll` where the PC can show it),
  decoy DLLs in the current folder and on `PATH`, a non-ASCII user folder, the
  Mark of the Web on a package and on a folder, a path over 260 characters,
  upgrade and uninstall, and Rack's screenshot mode where the PC gives Rack
  OpenGL 2.0.
- **CI**: `windows` (native build, CTest, host tests) and
  `windows-release-candidate` (build and verify), outside pull requests;
  evidence only, never binaries. `workflow_dispatch` takes `windows_tests`, a
  `-k` selection for the host tests.
- **The test suite runs on Windows**: every native and host test runs there
  unless it needs a Linux-only mechanism or a window. Platform facts live in
  `tests.support` (`WINDOWS`, `MODULE`, `bin_command`, `posix_shell`,
  `kill_tree`, `sanitizer_platforms`); symbol checks read PE as well as ELF and
  Mach-O.
- **Docs**: [docs/installation/windows.md](../docs/installation/windows.md),
  whose troubleshooting table quotes only messages seen in these Windows runs;
  the support tables in `docs/installation.md` and `docs/compatibility.md`;
  CONTRIBUTING.md and ADR 005.

## What Windows found, and the fixes

| Found on Windows | Cause | Fix |
| --- | --- | --- |
| Rack's own folder on `sys.path`, where a `.py` file could shadow the standard library or `rackpy` (`d570bc2`) | CPython's Windows path calculation adds the executable's folder | The search path is set explicitly; `test_security` checks it |
| Headless Rack quit while pytest ran inside it (`d570bc2`) | Rack (msvcrt) and CPython (UCRT) have separate descriptors 0 to 2 over one console handle; pytest's `--capture=fd` closed Rack's stdin | In-Rack runs use `--capture=sys` on Windows; documented for plugin authors |
| `upstream/api-surface.json` looked stale, and artifacts would have carried CRLF copies of Python files (`d570bc2`) | Git for Windows checks text out with CRLF | `.gitattributes`: `* text=auto eol=lf` |
| `_wmi.pyd` refused by the PE check (`d570bc2`) | It imports `propsys.dll` | Added to the Windows DLL allowlist |
| Under Wine, before any CI run: the loader refused with an unnamed exception (`5e64721`) | `rack::system::getCanonical` uses MinGW libstdc++'s `std::filesystem::canonical`, which threw `std::logic_error` for a `Z:` path; Rack catches only `filesystem_error` | `dynlib::canonicalPath` (`GetFinalPathNameByHandleW` on Windows) fails closed with a reason; the loader names any `std::exception` |
| The test programs did not build with MinGW (`a7bbcd4`, `e1f8ade`) | No `roundevenf` in MinGW's C library; GCC 16.2's `-Wmismatched-new-delete` on the test's replacement allocators | `roundevenf` as `nearbyintf` in the SIMD reference; a scoped pragma |
| RackPy refused to start in a user folder with a non-ASCII path (`4b35f5f`) | jansson's `json_load_file` opens paths with msvcrt's `fopen`, in the ANSI code page | `rackpy::loadJsonFile`: Rack's `system::readFile` (UTF-16) and `json_loadb`; native test |
| A damaged `python314.dll` hung Rack (`4b35f5f`) | `LoadLibraryExW` on a bad image shows a modal dialog | `SetThreadErrorMode(SEM_FAILCRITICALERRORS)` around the load |
| `verify.ps1` could not read Rack's log (`75aa4d6`) | Rack holds `log.txt` open for writing | Read with `FileShare.ReadWrite` |
| The loaded-DLL checks saw nothing, then everything as foreign (`4b35f5f`, `477b614`, `80cb7f7`) | `Process.Modules` returned no file names; one snapshot came after headless Rack had already passed "Press enter to exit"; `@($a + "\", ...)` is one string in PowerShell; `python3.dll` (CPython's stable-ABI forwarder) counted as a second CPython | Module lists every 100 ms while Rack runs; parenthesized prefixes; one `python3NN.dll`, every `python3*.dll` the bundled one |
| In the non-ASCII user folder, the DLL lists matched no folder (`fda5452`) | Toolhelp gave `Ünïcødé 音楽 ΑΩ` as `Ünïcødé ?? ?O`; its `szExePath` also holds only 260 characters. RackPy's own second-CPython check used the same call, so a CPython DLL deeper than 260 characters could have been missed | `verify.ps1` uses `EnumProcessModulesEx` and `GetModuleFileNameExW`; the loader `EnumProcessModules` and `GetModuleFileNameW` |
| The Windows runtime ships `__pycache__` (79 folders) | python-build-standalone precompiles the stdlib | Tests check that Rack writes no new `.pyc`, which is what they mean |
| ASan, TSan and the `LD_PRELOAD` guard do not exist here | MinGW GCC has no sanitizers; no `LD_PRELOAD` on Windows | Skipped with that reason (open gates below) |

## Results

CI run [41308926685](https://github.com/feoh/RackPy/actions/runs/41308926685),
commit `fda5452`: all 15 jobs passed, Linux and both Macs included. Evidence
kept locally under `~/.cache/rackpy/evidence/rp41/run-41308926685/` (both
Windows job logs, `manifest.json`, `SHA256SUMS` and `verify.ps1`'s logs);
CI keeps it 30 days.

### Release candidate (job 124855045120)

| | |
| --- | --- |
| Machine | Windows Server 2025 Datacenter 10.0.26100, x64; `LongPathsEnabled=1`; a Visual C++ redistributable in System32 |
| Built with | MSYS2 MINGW64 GCC 16.2.0 (msvcrt); CPython 3.14.9 (python-build-standalone 20261021, `x86_64-pc-windows-msvc`); Rack SDK 2.6.6 win-x64 |
| Artifacts | 14 (10 example plugins, the runtime, the wheel, the source archive, `manifest.json`); 52 PE files described in the manifest |
| Windows floor | 10 |
| Checks passed | 221 |
| Failed | 0 |
| OPEN (not run, not passed) | 10: each example's windowed render (no OpenGL 2.0) |
| Error cases | another loader ABI, an incomplete runtime, a damaged `python314.dll` (error 193), an unreadable one (error 5), two RackPy plugins: each refused with its reason, Rack exits cleanly |
| Decoy DLLs in Rack's current folder and on `PATH` | none loaded |
| Non-ASCII user folder (`Ünïcødé 音楽 ΑΩ`) | loads, runs, finalizes; DLLs all Rack's, the plugin's or Windows' |
| Mark of the Web | a marked package's extracted files carry none; a folder of marked files loads (recorded, not asserted) |
| A 368-character path | Rack killed at the timeout without loading the plugin (recorded, not asserted) |
| A missing `vcruntime140.dll` | not shown: System32 has one |

```text
707fdb6b904660231fc9c8e7c9c7b73e80f873283e2813cbc8b7e7e59dd97ac4  rackpy-0.0.0.dev0+gfda54529a-source.tar.gz
d823203048fc43b945f1b76af3b8bc536a8e6a3b8a725601ed4dc9f557080b72  rackpy-0.0.0.dev0-py3-none-any.whl
666bc4a94325bdd8fbc85cfdb27fe31517c9bc3933e8c37389abccfdb4eee193  rackpy-runtime-0.0.0.dev0+gfda54529a-win-x64.tar.zst
```

The wheel is byte-identical to the one both Macs built at `e1d5b4f`
(RP-40): the Python package did not change, and the wheel build is
reproducible across the Mac and Windows build machines. The plugin packages' checksums
are in the evidence folder's `SHA256SUMS`.

### Native and host tests (job 124855045282)

`pytest -m "(native or host) and not dist and not sanitizer and not stress and not inventory"`;
CTest through the `dev_build` fixture: 8 of 8.

| Run | Commit | Host tests | `verify.ps1` |
| --- | --- | --- | --- |
| [41279230675](https://github.com/feoh/RackPy/actions/runs/41279230675) | `5e64721` | 62 passed, 36 failed, 111 skipped, 2 errors | stopped: `_wmi.pyd` imports `propsys.dll` |
| [41284176925](https://github.com/feoh/RackPy/actions/runs/41284176925) | `d570bc2` | 89 passed, 3 failed, 119 skipped | stopped: Rack's open `log.txt` |
| [41288915853](https://github.com/feoh/RackPy/actions/runs/41288915853) | `75aa4d6` | 91 passed, 1 failed, 119 skipped | every example loaded, ran, saved and reopened; the DLL checks saw no module list; the non-ASCII folder refused; a damaged DLL hung Rack |
| [41292904958](https://github.com/feoh/RackPy/actions/runs/41292904958) | `4b35f5f` | 92 passed, 0 failed, 119 skipped | 26 failed, all loaded-DLL checks (snapshot of an exiting Rack) |
| [41298939078](https://github.com/feoh/RackPy/actions/runs/41298939078) | `477b614` | the smoke test only: 1 passed | the same 26 (the checks' own bugs) |
| [41302010771](https://github.com/feoh/RackPy/actions/runs/41302010771) | `80cb7f7` | 92 passed, 0 failed, 119 skipped | 219 passed, 2 failed (non-ASCII folder's DLL list) |
| [41308926685](https://github.com/feoh/RackPy/actions/runs/41308926685) | `fda5452` | 92 passed, 0 failed, 119 skipped | 221 passed, 0 failed, 10 OPEN |

The 119 skips: 81 windowed tests (`windows-opengl`, an open gate below); 34
Linux-only (the `LD_PRELOAD` guards, ELF and official-toolchain checks, and
the RP-04, RP-06 and RP-07 spikes); 4 sanitizer programs (MinGW GCC has no
ASan or TSan).

### Linux, after the Windows changes

The changes touch code every platform shares (`loadJsonFile` in the loader,
host and model manifest; `dynlib`; the tests), so the whole Linux suite ran
again on the final tree (`fda5452` plus these docs): `pytest -m "not sanitizer
and not stress"`, 497 passed, 0 failed, 16 deselected (the opt-in sanitizer and
stress suites), in 26 min 35 s on the i9-14900K workstation; CTest 8/8 through
the `dev_build` fixture. The docs build with `-W`. Log:
`~/.cache/rackpy/evidence/rp41/linux-full-suite-fda5452.log`.

## Open gates (not waived)

- **Windowed rendering on Windows**: module widgets drawn in a window, the
  browser preview, Rack's screenshot mode. GitHub-hosted runners have only
  Windows' OpenGL 1.1 and Rack needs 2.0; this needs a PC with a GPU driver.
  81 host tests skip with `RACKPY_ALLOW_MISSING=windows-opengl`, in CI only,
  and `verify.ps1` reports each example's render as OPEN.
- **A clean PC**: the runners have developer tools, other Pythons and a
  Visual C++ redistributable in System32, so a missing `vcruntime140.dll`
  could not be shown there. `verify.ps1` records which DLLs Rack actually
  loaded instead.
- **Authenticode signing**: RackPy has no code-signing certificate; signing
  needs the owner's credentials and approval.
- **Sanitizers on Windows**: MinGW GCC has no AddressSanitizer or
  ThreadSanitizer; those suites run on Linux.
- **The audio-path guard on Windows**: the guard interposes allocation,
  locks, writes and the GIL through `LD_PRELOAD`, which Windows lacks. Only
  the static half (imports of the kernel DLLs, `audio-symbols` on PE) runs
  here. Belongs to RP-43.
- **Allocation-failure injection into `libRack.dll`**: libRack.dll allocates
  through its own `libstdc++-6.dll`, whose `operator new` a PE executable
  cannot replace, so on Windows `widget_tree` checks that the injection did
  not happen and goes on. The rollback it tests is RackPy's own,
  platform-independent code, checked on Linux and macOS.
- **Paths over 260 characters**: Rack itself fails there, even with
  `LongPathsEnabled=1` (recorded, not asserted); the install guide advises a
  short user folder.
- **Sound cards and MIDI devices**: as on Linux and macOS, not part of these
  checks.
