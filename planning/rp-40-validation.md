# RP-40 validation: macOS artifacts with native architecture evidence

Task: `tk-rackpy-40-package-and-validate-macos-artifacts-w-719807`, branch `rp40`.
Everything below ran natively on GitHub-hosted macOS runners, one per
architecture: `macos-15` (Apple silicon, arm64) and `macos-15-intel` (x86_64),
with Xcode's AppleClang. The Zig cross-build on Linux (`--toolchain zig-macos`)
is a compile and link check only: its output was inspected, never run or
shipped. Rack Free 2.6.6 (`061ccf63c1758599396ac1bb10d47345d9d34076`), the
universal `RackFree-2.6.6-mac-x64+arm64.pkg`. Nothing was uploaded, published,
signed with a Developer ID or notarized.

## What was delivered

- **The plugin builds for each Mac architecture** (`CMakeLists.txt`,
  `cmake/RackSDK.cmake`): `plugin.dylib`, `rackpy_host.so` and kernel libraries
  as MH_DYLIB files, one architecture per build (no universal binaries),
  deployment targets 11.0 (arm64) and 10.15 (x86_64), the pinned CPython for
  each architecture (`packaging/inputs.json` schema 2), `@rpath/<file>` install
  names.
- **`bin/rackpy-release build` on a Mac** strips (`strip -S -x`) and signs ad
  hoc as Rack's `make dist` does, and fails on any Mach-O file that is for the
  other CPU or universal, loads anything but macOS's libraries, Rack's `libRack`
  (the loader only) or the bundled runtime, searches another path, needs a
  newer macOS than claimed, or whose signature does not verify. The manifest
  records each binary's facts (`mach_o`) and the compiler, SDK and macOS.
- **`bin/rackpy-release verify` on a Mac** runs `packaging/macos/verify.sh`
  with an empty environment: fresh user folders, every example headless twice
  (save, reopen), the images Rack mapped (only the plugin's own Python;
  nothing from Homebrew, `/usr/local`, a Python framework or a home folder),
  the Linux error cases, macOS's own cases (a library for the other CPU, an
  unsigned loader then re-signed, quarantined package and folder), upgrade and
  uninstall, and Rack's screenshot mode (`-t`) for a windowed render of every
  model where the Mac gives Rack an OpenGL window.
- **CI**: `macos` (native build, CTest, host tests) and
  `macos-release-candidate` (build and verify) on both architectures; evidence
  only, never artifacts. `workflow_dispatch` takes `macos_tests`, a `-k`
  selection for the host tests.
- **The test suite runs on macOS** (`9f57d1f`, `84ebcf2`): every native and
  host test runs there unless it needs a Linux-only mechanism, and those are
  marked one by one (`linux_x64_only`), not by file. Still Linux-only: the
  `LD_PRELOAD` audio-path guard runs, the sanitizer and stress programs, the
  glibc floor, the official Linux toolchain (`dist`), ELF inspection, and the
  RP-04, RP-06 and RP-07 spikes (`tests/integration/`), which ADR 005 keeps as
  the evidence for their design decisions: their own Makefiles, Linux-only
  pinned inputs, Xvfb and `LD_PRELOAD`. Their questions (embedding, thread
  affinity, teardown, kernels matching direct C++) are answered on macOS by the
  production host tests instead. Symbol checks compare Itanium-mangled names
  (`tests.support.nested_name`), so they read Mach-O and ELF alike.
- **Docs**: [docs/installation/macos.md](../docs/installation/macos.md), the
  quickstart's Mac folders and tools, and corrections found on macOS
  (`docs/reference/simd.md`'s `sin(±inf)`).

## What macOS found, and the fixes

Each was invisible on Linux; each now has a test that runs on every platform
where that is possible.

| Found on macOS | Cause | Fix |
| --- | --- | --- |
| A graph plan one ulp from its C++ reference after a phase wrap (`7e98051`) | LLVM turned the reference's `x * (1.f / rate)` into `x / rate`; modules multiply by Rack's stored `sampleTime` | The references multiply by a stored sample time |
| Every Rack test errored at setup (`f06702c`) | macOS's `bsdtar` did not expand Rack's installer payload | `pkgutil --expand-full` on a Mac |
| The host refused to load: `tried: '/tmp/Rack2/libRack.dylib' (no such file)` (`a9961af`) | Rack symlinks `/tmp/Rack2` only while it dlopens `plugin.dylib`; dyld looks the path up where Linux's ld.so matches by name | Only `plugin.dylib` links `libRack` by path; the host and kernels resolve Rack through `-undefined dynamic_lookup`; the build and `inspect` enforce it |
| `generic_type: type "Driver" is already registered!` (`11d909a`) | Off libstdc++ pybind11 keys types by `typeid(T).name()`, and anonymous-namespace classes of the same name collide | Unique names; `tests/native/test_local_types.py` |
| `wrong native Widget base for this method` (`6ab9a01`) | 633 Rack classes without a key function have a hidden `type_info` in libRack and another in the host; libc++abi's `dynamic_cast` compares addresses | `rackpy::rttiCast` (name-based Itanium ABI fallback) for all 122 casts; CTest `rtti`; a unit test forbids plain `dynamic_cast` |
| `rack::Exception` arrived as `RuntimeError` (`e1d5b4f`) | The same duplicate `type_info`, in a `catch` clause | Catch `std::exception`, identify Rack's with `rttiCast` |
| One-ulp differences in inline SDK math on arm64 (`e1d5b4f`) | Clang fused `a*b+c` into FMA in some places only | `-ffp-contract=off`: no FMA on every platform |
| `sse_mathfun_sincos_ps`'s sine one ulp from `sse_mathfun_sin_ps` on arm64, once FMA was off (run 41259278380) | The same polynomial inlined twice; `-funsafe-math-optimizations` still allows reassociation (inferred: clang 23 emits no FMA with contraction off) | The test compares within float precision, not bit for bit |
| `sin(±inf)` of a vector is NaN, not `∓inf` (`e1d5b4f`) | `sse_mathfun` is header code: GCC and Clang differ | Documented as compiler-dependent; the test records each platform's result |
| Every windowed Rack run hung (`5e2a5ed`) | The runners give Rack no OpenGL window (GLFW 65545), and Rack waits on a modal dialog | A one-off probe; windowed tests skip with `RACKPY_ALLOW_MISSING=macos-opengl` in CI only; `verify` reports them OPEN |
| Apple's ASan refuses `detect_leaks=1` (`f06702c`) | No LeakSanitizer on macOS | Leaks are checked on Linux only |

## Results

CI run [41259278380](https://github.com/feoh/RackPy/actions/runs/41259278380),
commit `e1d5b4f`. Evidence kept locally under
`~/.cache/rackpy/evidence/rp40/run-41259278380/` (job logs, each
architecture's `manifest.json` and `SHA256SUMS`); CI keeps it 30 days.

### Release candidates

| | arm64 (job 124708970278) | x86_64 (job 124708970243) |
| --- | --- | --- |
| Machine | macOS 15.8.2 (24H232) | macOS 15.7.9 (24G830) |
| Gatekeeper / SIP | assessments enabled / disabled | assessments enabled / disabled |
| Artifacts | 13 | 13 |
| Deployment target | 11.0 | 10.15 |
| Checks passed | 214 | 214 |
| Failed | 0 | 0 |
| OPEN (not run, not passed) | 10: each example's windowed render | 10: each example's windowed render |
| A package for the other CPU | refused, with the reason in Rack's log; Rack exits cleanly | same |
| Unsigned `plugin.dylib` | loaded (recorded, not asserted) | loaded (recorded, not asserted) |
| Quarantined package | extracted files not quarantined; loads | same |
| Quarantined unpacked folder | loaded (recorded, not asserted) | loaded (recorded, not asserted) |

The source archive and wheel came out byte-identical on both Macs:

```text
75cc5f616beef9f16cdc13661671b0c3577cd651b2865d2a386497d6985514e8  rackpy-0.0.0.dev0+ge1d5b4f2e-source.tar.gz
d823203048fc43b945f1b76af3b8bc536a8e6a3b8a725601ed4dc9f557080b72  rackpy-0.0.0.dev0-py3-none-any.whl
```

The runtimes and plugin packages differ by architecture, as they must; each
architecture's `SHA256SUMS` is in the evidence folder.

### Native and host tests

`pytest -m "(native or host) and not dist and not sanitizer and not stress and not inventory"`, CTest
through the `dev_build` fixture (8 of 8 on both).

| Run | Commit | arm64 | x86_64 |
| --- | --- | --- | --- |
| [41259278380](https://github.com/feoh/RackPy/actions/runs/41259278380) | `e1d5b4f` | 69 passed, 1 failed (`sse_mathfun` sin vs sincos, fixed in `50bd3f5`), 140 skipped | 71 passed, 0 failed, 139 skipped |
| [41264853952](https://github.com/feoh/RackPy/actions/runs/41264853952) | `84ebcf2` | 91 passed, 4 failed (the newly ported CMake setup-error tests, fixed in `1799364`), 116 skipped | 92 passed, 4 failed (the same), 115 skipped |
| [41268251622](https://github.com/feoh/RackPy/actions/runs/41268251622) | `1799364` | the five setup-error tests and the SDK-symbol test (a `-k` selection): 6 passed | 6 passed |

In run 41268251622 both release candidates passed again, as in runs 41259278380 and 41264853952.

The skips on arm64 in run 41264853952: 81 windowed tests (`macos-opengl`, an open gate below); 34 Linux-only
(the `LD_PRELOAD` guards, ELF and official-toolchain checks, sanitizer programs, and the RP-04, RP-06 and
RP-07 spikes); 1 SSE4.2 comparison that arm64 cannot make (both builds use SIMDe there). Logs:
`~/.cache/rackpy/evidence/rp40/run-41264853952-*-host.log`.

### Linux, after the macOS changes

The fixes touch code every platform shares (`-ffp-contract=off`, `rttiCast`,
the exception translator, renamed local types), so the whole Linux suite ran
again at `9f57d1f`: `pytest -m "not sanitizer and not stress"`, 497 passed,
16 deselected (the opt-in sanitizer and stress suites), in 26 min 41 s on the
i9-14900K workstation. CTest passed 8/8 on a clean rebuild with the same native
code (only tests and `bin/rackpy-dev` changed after it). Log:
`~/.cache/rackpy/evidence/rp40/linux-full-suite-9f57d1f.log`.

## Open gates (not waived)

- **Windowed rendering on macOS**: module widgets drawn in a window, the
  browser preview, window recreation. GitHub-hosted runners give Rack no OpenGL
  window; this needs a Mac with a GPU and a logged-in session.
- **Gatekeeper and signing on a personal Mac**: the runners had Gatekeeper on
  and SIP off, and loaded both an unsigned `plugin.dylib` and a quarantined
  plugin folder. On Apple silicon an unsigned library normally does not run at
  all, so the runners enforce code signing less strictly than a stock Mac
  (inferred from that result and SIP being off; not measured). What a Mac with
  SIP on does with a quarantined folder is not established. RackPy has no Developer ID; signing and notarization need the
  owner's credentials and approval.
- **A clean Mac**: runners have Homebrew and other Pythons installed; the
  verify script shows which libraries Rack actually mapped instead.
- **Leak checking on macOS**: Apple's ASan has no LeakSanitizer.
- **The audio-path guard on macOS**: the guard interposes `malloc`, locks,
  writes and the GIL through `LD_PRELOAD`; macOS would need
  `DYLD_INSERT_LIBRARIES` and two-level-namespace interposing. On macOS only
  the static half runs (no CPython symbol in kernels, I/O and lifecycle
  objects). Belongs to RP-43.
- **Sound cards and MIDI devices**: as on Linux, not part of these checks.
