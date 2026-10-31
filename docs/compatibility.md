# Compatibility baseline

**RP-02 record, verified 2026-10-21.** This page pins what RackPy targets and
records what has been checked so far. Compatibility with a running Rack host
has **not** been tested. Nothing here says a RackPy runtime exists.

Machine-readable pins are in [upstream/versions.json](../upstream/versions.json).
Recorded digests, the include closure and a preliminary declaration survey are
in [tests/fixtures/sdk-provenance.json](../tests/fixtures/sdk-provenance.json).
Both are checked by [`bin/rack-sdk`](../bin/rack-sdk).

## Rack baseline: keep 2.6.6

| Fact | Evidence |
| --- | --- |
| `v2.6.6` is the newest v2 tag | `git ls-remote --tags https://github.com/VCVRack/Rack.git` |
| The tag is lightweight and names commit `061ccf63c1758599396ac1bb10d47345d9d34076` | `gh api repos/VCVRack/Rack/git/ref/tags/v2.6.6` |
| `refs/heads/v2` and `HEAD` also pointed at that commit on 2026-10-21 | `git ls-remote https://github.com/VCVRack/Rack.git refs/heads/v2 HEAD` |
| SDK headers equal the source tree's `include/` at that commit | `diff -r` of a shallow checkout against the extracted lin-x64 SDK: no differences |
| No GitHub releases exist; binaries come from vcvrack.com | `gh release list -R VCVRack/Rack` is empty |

**Decision:** keep the research baseline. RackPy targets the commit, not the
mutable `v2` branch. A new Rack release means an inventory diff and owner-visible
notes (see [coverage](coverage.md#release-bar)), not a version edit.

## SDK archives

The download URLs are mutable vendor URLs, so the SHA-256 is the pin. Each
archive was downloaded twice into empty directories and gave identical digests.
`bin/rack-sdk verify` re-downloaded all four into a fresh cache and matched.

| Platform | Archive SHA-256 | Library shipped |
| --- | --- | --- |
| lin-x64 | `420da245…18532` | `libRack.so` (`44850207…97ba7`) |
| mac-x64 | `9b8b0d75…060ec` | `libRack.dylib` (`43c6abb8…c7ebd`) |
| mac-arm64 | `29414e52…0481f` | `libRack.dylib` (`55dd40f7…aaf1c`) |
| win-x64 | `12f20433…5a05c` | `libRack.dll.a`, a MinGW import library |

Digests are shortened here; full values are in `upstream/versions.json` and the fixture.

**No SDK exists** for `lin-arm64` or `win-arm64`. Both URLs return a 404 page.

What differs between the four archives:

- `include/` is byte-identical on every platform. The macOS archives also
  contain a Finder `.DS_Store`, which the tree digests exclude.
- `dep/include/` differs only in `openssl/configuration.h`. OpenSSL headers are
  not reachable from `rack.hpp`.
- `arch.mk`, `compile.mk`, `plugin.mk`, `dep.mk` and `helper.py` are identical.
- Only the platform library differs.

So the declaration text is the same everywhere. Platform differences come from
preprocessor branches and system headers, listed under
[platform branches](#platform-branches-in-the-public-headers).

## Host binaries for later tests

RP-04 and later tasks need an actual Rack host. These Rack Free archives are
pinned for that and are not redistributed:

| Platform | File | SHA-256 |
| --- | --- | --- |
| lin-x64 | `RackFree-2.6.6-lin-x64.zip` | `69acf0c9…0fc6e` |
| macOS universal | `RackFree-2.6.6-mac-x64+arm64.pkg` | `5ecbe5c0…3096c` |
| win-x64 | `RackFree-2.6.6-win-x64.exe` | `2852c52c…6111c` |

The Rack Free 2.6.6 Linux host's `libRack.so` is **byte-identical** to the SDK's.

The planning machine also has VCV Rack **Pro** 2.6.6 (standalone, Linux x64).
Its `libRack.so` differs from the SDK's (`d63b9b92…8f413`). A result from Rack Pro
is therefore not evidence for Rack Free, and DAW-plugin hosting stays a non-goal.

## Build and ABI constraints from the SDK

These come from the SDK makefiles and the shipped libraries. They describe what
Rack expects of a plugin, not anything RackPy has shipped.

| Constraint | Linux x64 | macOS x64 / arm64 | Windows x64 |
| --- | --- | --- | --- |
| Language mode in `compile.mk` | `-std=c++11` | `-std=c++11 -stdlib=libc++` | `-std=c++11` |
| CPU flags | `-march=nehalem` (SSE4.2) | `-march=nehalem` / `-march=armv8-a+fp+simd` | `-march=nehalem` |
| Plugin file | `plugin.so` | `plugin.dylib` | `plugin.dll` |
| Linking to Rack | `-lRack`, `RUNPATH=/tmp/Rack2` (Rack symlinks its system dir there) | `-undefined dynamic_lookup`; `dist` rewrites the install name to `/tmp/Rack2/libRack.dylib` | `-lRack` against `libRack.dll.a` |
| C++ runtime | `-static-libstdc++ -static-libgcc` | system `libc++` | `-static-libstdc++` |
| Other required flags | `-fno-gnu-unique` (GCC-only) | `-mmacosx-version-min=10.9` | `-D_USE_MATH_DEFINES -municode` |
| libRack minimum OS | glibc symbols ≤ `GLIBC_2.17` | x64: macOS 10.9, arm64: macOS 11.0 (built with SDK 26.0) | not inspectable: SDK has no DLL |
| libRack compiler | GCC 15.2.0 (`.comment`) | not recorded | MinGW (COFF import library) |
| Signing | none | `codesign -f -s -` ad hoc by default | none |

Official cross toolchain: [rack-plugin-toolchain](https://github.com/VCVRack/rack-plugin-toolchain)
branch `v2` at `4fd13187`, pinned to SDK 2.6.6. It builds crosstool-ng
`x86_64-ubuntu16.04-linux-gnu`, mingw-w64 v10.0.0 and osxcross with clang
20.1.1 and the macOS 12.3 SDK. RP-08 built the **Linux** toolchain with
`bin/rack-toolchain build` (GCC 15.1.0, sysroot glibc 2.23); the Windows and
macOS toolchains have not been built.

**Selected toolchains.**

- *Development and validation on Linux x64:* the host GCC, driven by the
  SDK's own `plugin.mk` so that the flags are Rack's. RP-02 validated the
  rack.hpp-only probe this way.
- *Distributable artifacts:* the official toolchain above. For Linux, RP-08
  pinned it (commit, `Makefile` SHA-256, a digest-pinned Docker build image),
  built it and checked it: the skeleton built with it needs at most
  `GLIBC_2.14`, and the bundled `libpython` needs `GLIBC_2.17`
  ([ADR 005](decisions/005-build-and-ci.md)).

### Findings that constrain later tasks

1. **A host-toolchain Linux build is not distributable.** The probe built with
   the host GCC needs `GLIBC_2.35`; libRack itself needs only `GLIBC_2.17`.
   Linux release artifacts need the official toolchain or an equivalent old-glibc
   sysroot. Host builds are fine for development and tests. RP-08 builds with
   the official toolchain in CI and checks the floor with
   `bin/rackpy-dev inspect --max-glibc 2.23`. RP-39's release candidates
   (`bin/rackpy-release build`) are built this way, and run on clean
   Ubuntu 16.04 (glibc 2.23) and 20.04 containers
   ([install on Linux](installation/linux.md)).
2. **Windows mixes C runtimes unless proven otherwise.** Plugins link against
   a MinGW import library. python.org CPython is built with MSVC. RP-04 and
   RP-41 must probe CRT and C++ runtime coexistence before choosing an ABI.
3. **Headers accept newer C++.** `rack.hpp` parses with GCC 16.2.1 and clang
   23.1.3 under `-std=c++17`, `c++20` and `c++23` with no errors. Under C++20,
   GCC warns about implicit `this` capture in `helpers.hpp`. Header parsing does
   not prove ABI compatibility with a C++11-built libRack. RP-04 must check that
   before a C++17 binding library such as nanobind is chosen.
4. **SIMD is platform-conditional.** `simd/common.hpp` uses `<nmmintrin.h>` when
   `__SSE4_2__` is defined and SIMDe emulation otherwise. The x64 flags select
   the native path; arm64 uses SIMDe. Binding and tests for `simd::` must run on
   both. (RP-20)
5. **Clang needs one SDK flag changed.** `-fno-gnu-unique` is GCC-only, and the
   SDK's `-Wno-vla-extension` does not silence clang 23's
   `-Wvla-cxx-extension` in `dsp/ode.hpp`. The libclang inventory (RP-05) has to
   use adjusted flags and record them.

## Platform branches in the public headers

The include closure contains only these platform conditionals:

| Header | Branch | Effect on the public surface |
| --- | --- | --- |
| `arch.hpp` | `__x86_64__`, `__aarch64__`, `_WIN32`, `__APPLE__`, `__linux__` | Defines `ARCH_X64`/`ARCH_ARM64` and `ARCH_WIN`/`ARCH_MAC`/`ARCH_LIN` |
| `common.hpp` | `ARCH_WIN` | Redirects `fopen` to `fopen_u8`, asserts 2-byte `wchar_t` |
| `string.hpp` | `ARCH_WIN` | Declares `string::UTF16toUTF8` and `string::UTF8toUTF16` on Windows only |
| `widget/event.hpp` | `ARCH_MAC` | `RACK_MOD_CTRL` is `GLFW_MOD_SUPER` and `RACK_MOD_CTRL_NAME` is `⌘` on macOS |
| `simd/common.hpp` | `__SSE4_2__` | Native SSE4.2 intrinsics or SIMDe |

`rtaudio.hpp` also branches on `ARCH_MAC`, but it is not in the closure.
System headers differ per platform as well. The Linux extraction resolved 303
system headers. macOS and Windows extraction needs native SDKs or sysroots,
which this machine does not have. RP-05 records that gap.

## Python candidates

RP-04 chose **CPython 3.14** (full ABI, with the GIL) from python-build-standalone
`20261021` as the first runtime, with 3.15 next. Both passed the Linux x64 host
tests ([ADR 002](decisions/002-embedding-and-bindings.md)). As of 2026-10-21
([devguide](https://devguide.python.org/versions/)):

| Branch | Status | Candidate for embedding? |
| --- | --- | --- |
| 3.15 | bugfix, first released 2026-10-09 | yes |
| 3.14 | bugfix | yes |
| 3.13, 3.12, 3.11 | security fixes only | fallback only: python.org publishes no new binary installers for these branches |
| 3.10 | end of life on 2026-10-01 | no |

These are not assumed: the free-threaded build, the limited/stable ABI,
subinterpreters, or a particular distribution source (python.org, the
python-build-standalone builds that uv uses, or system Python). Each needs RP-04
evidence on a real host.

## Validation targets

| Target | SDK | Status |
| --- | --- | --- |
| Linux x86_64 | yes | **Initial validation target.** SDK verified; the RP-08 skeleton (no bindings) builds and loads in Rack Free 2.6.6, headless and in Xvfb. No release artifact. |
| macOS arm64 | yes | Candidate. Headers verified; **nothing built or run**. Needs native hardware. |
| macOS x86_64 | yes | Candidate. Headers verified; **nothing built or run**. Needs native hardware. |
| Windows x86_64 | yes | Candidate. Headers verified; **nothing built or run**. CRT risk above. |
| Linux arm64 | **no** | Not a target: no SDK published. |
| Windows arm64 | **no** | Not a target: no SDK published. |

Cross-compiling does not establish support for a target. Only native execution
in a real Rack host does (RP-39, RP-40, RP-41).

## Reproduce

```bash
bin/rack-sdk fetch --fresh   # download all four SDKs into new empty dirs; check SHA-256
bin/rack-sdk verify          # archive, header-tree and library digests
bin/rack-sdk closure         # Linux x64 rack.hpp include closure versus the fixture
bin/rack-sdk survey          # preliminary declaration survey versus the fixture
bin/rack-sdk probe           # build tests/fixtures/sdk-probe with the SDK's plugin.mk
bin/rack-sdk environment     # host facts to record with a run
```

The script is a [PEP 723](https://peps.python.org/pep-0723/) uv script and runs
without a project environment. `closure` needs GCC, `survey` needs clang, and
`probe` needs GNU Make, GCC, `jq` (the SDK's `plugin.mk` reads `plugin.json` with
it) and binutils. Downloads go to `$XDG_CACHE_HOME/rackpy/sdk`, or
`--cache DIR`. Pass `--write` to `closure` or `survey` to regenerate the fixture,
and review the diff before committing it.

### Results on the planning machine

CachyOS, Linux 7.2.11, x86_64, glibc 2.44, GCC 16.2.1, clang 23.1.3,
GNU Make 4.4.1, uv-managed CPython 3.14.2 for the script.

- `verify`: all four archives, header trees and libraries matched from an
  empty cache. A changed byte in an extracted header made `verify` and `closure`
  fail.
- `probe`: built with no warnings, exports `init`, needs `libRack.so`, `libm`
  and `libc`, and has `RUNPATH /tmp/Rack2`. It needs at most `GLIBC_2.35`.
- `closure`: 112 Rack headers and 13 dependency headers reached; 12 shipped
  Rack headers not reached.
- `survey`: deterministic across repeated runs. Its counts are navigation data,
  not coverage. See [coverage](coverage.md#pinned-surface-rp-02).
