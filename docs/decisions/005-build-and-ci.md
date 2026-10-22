# ADR 005: Build system, pins, test policy and CI

- **Status:** Accepted for Linux x86_64. macOS and Windows have no build yet
  (RP-40, RP-41).
- **Date:** 2026-10-22
- **Task:** RP-08
- **Evidence:** [tests/integration/test_host_smoke.py](../../tests/integration/test_host_smoke.py),
  [tests/integration/test_dist_build.py](../../tests/integration/test_dist_build.py),
  [tests/native/test_build.py](../../tests/native/test_build.py),
  [.github/workflows/ci.yml](../../.github/workflows/ci.yml)

## Context

RP-04 to RP-07 proved the embedding, lifetime and performance designs with
spike code. RP-08 turns the validated parts into a production skeleton that
builds reproducibly and loads into a real Rack, and puts checks around it. The
skeleton binds **no Rack API**: it starts the bundled interpreter in Rack,
imports `rackpy`, optionally imports one trusted module, and finalizes at
shutdown. Its plugin registers no modules.

## Decisions

### 1. CMake and Ninja build the plugin; uv builds only the Python package

The product is a Rack plugin directory, not a Python extension. Its host
library links the **bundled** python-build-standalone `libpython`, which is not
the interpreter that runs the build, and a wheel built against the build
interpreter could not resolve Rack's symbols outside Rack anyway
([architecture](../architecture.md#runtime-sketch)). So:

- `CMakeLists.txt` (CMake ≥ 3.25, Ninja) builds the loader `plugin.so`, the
  host `rackpy_host.so` and native unit tests. `bin/rackpy-dev` fetches the
  pinned inputs and drives it.
- `pyproject.toml` builds the pure-Python `rackpy` package with `uv_build` for
  the development environment, typing and unit tests. The plugin ships the same
  sources under `pythonpath/`.
- scikit-build-core is not used. It would be worth reconsidering if RP-39
  ships a developer wheel with native code.
- pybind11 3.1.0 (ADR 002) comes from `uv.lock`, headers only
  (`PYBIND11_NOPYTHON`). Its wheel hash in `uv.lock` equals the spike's pin.

### 2. Everything is pinned and verified

| Input | Pin | Verified by |
| --- | --- | --- |
| Rack SDK 2.6.6 | `upstream/versions.json`, `tests/fixtures/sdk-provenance.json` | `bin/rack-sdk`; CMake also checks `libRack.so` and all 125 headers reachable from `rack.hpp` and refuses anything else |
| Bundled CPython 3.14.9 | `packaging/inputs.json` (python-build-standalone `20261021`) | `bin/rackpy-dev inputs` (SHA-256) |
| pybind11, ruff, mypy, pytest, cyclopts, clang bindings | `uv.lock`, resolved with `exclude-newer = 2026-10-21` | `uv sync --frozen` |
| uv | 0.9.18 in CI; `required-version >= 0.9.18` | setup-uv |
| Official Linux toolchain | rack-plugin-toolchain `4fd13187`, its `Makefile` SHA-256, a digest-pinned `ubuntu:24.04` build image | `bin/rack-toolchain build` and `check` |
| GitHub Actions | commit SHAs | the workflow |

Downloads and builds stay outside git (`$XDG_CACHE_HOME/rackpy`, `build/`).
The toolchain image's apt packages are not pinned by version; the toolchain it
produces is checked instead (compiler version, sysroot glibc).

### 3. The SDK's flags, with recorded differences

`cmake/RackSDK.cmake` reproduces `compile.mk` and `plugin.mk` for Linux x64:
`-fPIC -g -O3 -funsafe-math-optimizations -fno-omit-frame-pointer
-march=nehalem -Wall -Wextra -Wno-unused-parameter -Wsuggest-override
-fno-gnu-unique`, `-lRack`, `RUNPATH /tmp/Rack2`, static libstdc++ and libgcc.
The differences are deliberate:

- SDK headers are `-isystem`, as ADR 004 requires (GCC 16's false
  `-Warray-bounds` in `Port::setChannels`). `-Wno-vla-extension` is then moot.
- `CMAKE_BUILD_TYPE` must be empty, so CMake adds no optimization flags of its own.
- The loader is C++11 like the SDK; the host is C++17 (ADR 002).
- The host uses hidden visibility and a **version script** that exports only
  `rackpy_host_init` and `rackpy_host_destroy`. Hidden visibility alone left
  about 270 symbols exported: libstdc++ gives `std::` instances and its static
  objects default visibility.
- `-ffile-prefix-map` keeps the checkout path out of log lines and debug info.
- `RACKPY_WERROR=ON` (CI and the tests) turns warnings in RackPy's own sources into errors.

### 4. Plugin layout and host ABI 1

```text
RackPy/
  plugin.so            loader; no Python dependency
  rackpy_host.so       embeds CPython; RUNPATH /tmp/Rack2:$ORIGIN/python/lib
  rackpy-loader.json   host, libpython, stdlib marker, python_path, entry
  plugin.json          slug RackPy, version 2.0.0, no modules
  pythonpath/rackpy/   the rackpy package (and any trusted plugin code)
  python/              the bundled runtime (linked for tests, copied for distribution)
  LICENSE, NOTICE
```

The loader validates `rackpy-loader.json` (relative paths without `..`, a
dotted module name, ABI 1) and keeps the RP-04 order: refuse a second CPython,
check the runtime, open `libpython` `RTLD_GLOBAL | RTLD_NODELETE`, open the host
`RTLD_LOCAL | RTLD_NODELETE`. `native/include/rackpy/host_abi.h` is the C ABI.
`rackpy._bootstrap` refuses a host whose ABI differs from its own. These names
are internal; RP-10 and RP-16 decide what plugin authors see.

### 5. A missing requirement fails; categories select CI jobs

Each test carries one of `unit`, `native`, `inventory`, `host` or `dist`
(`gui` additionally marks Xvfb tests); collection fails on an unmarked test.
Tests needing the SDK, Rack, clang, Xvfb or the toolchain fail with a fix-it
message when it is missing. `RACKPY_ALLOW_MISSING=<name>` turns that into a
reported skip, so a skip is always a visible choice. This replaced three silent
skips in the spike tests: no clang, no SDK and no `xvfb-run`. The preload test
used an Arch-specific system `libpython`; it now preloads the pinned 3.15
runtime, which is always available.

### 6. CI

One workflow, `ubuntu-24.04`, six jobs: planning data; hooks, strict typing and
unit tests; SDK pins, native build and the audio-path guard; the inventory;
real-host smoke tests (headless and Xvfb); and, outside pull requests, the
distributable build with the official toolchain (restored from cache, or built
in about an hour). Reports and Rack logs are kept 14 days. CI keeps no binaries,
because distributing one needs the RP-39 license manifest and owner approval.

### 7. Spike code is retained

The spikes are the evidence for accepted ADRs, and their tests run in CI:

| Directory | Kept because | Production successor |
| --- | --- | --- |
| `spikes/embedding` | ADR 002 evidence, including the nanobind comparison and the direct-link failure | `native/loader`, `native/host` |
| `spikes/lifecycle` | ADR 003 evidence; RP-10 implements its sentinel, lease and context rules | RP-10 |
| `benchmarks/spike` | ADR 004 baseline and the audio-path guard CI runs | RP-17 kernels; `bin/rackpy-dev audio-symbols` |

Only patterns those tests validated were promoted: the two-stage loader,
interpreter collision checks, the isolated `PyConfig`, GIL release after init,
and finalization in `destroy()`. The spike's binding demonstrations (value
types, handles, buffers, subclass transfer) are not promoted; the family tasks
implement them against the manifest.

## Results on the planning machine

CachyOS, Linux 7.2.11, Intel Core i9-14900K, GCC 16.2.1, CMake 4.4.4,
Ninja 1.13.2, uv 0.9.18, Rack Free 2.6.6 headless.

- `uv run pytest -m "not dist"`: **75 passed** in 141 s. `-m dist`: 2 passed.
- `uv sync --frozen` and `bin/rackpy-dev build --werror` succeed in a clean
  worktree; the build has no warnings; CTest passes.
- In Rack, with `PYTHONPATH` pointing at a planted module: isolated flags set,
  `sys.path` entirely inside the plugin, the planted module not importable,
  no bytecode written, `Py_FinalizeEx` returned 0, Rack exited 0. A missing
  runtime, an ABI mismatch, a failing entry module and a second RackPy plugin
  each produce their log message and a clean exit.
- **Official toolchain:** GCC 15.1.0, sysroot glibc 2.23, built in about 45
  minutes with 24 jobs. Built with it, `plugin.so` and `rackpy_host.so` need at
  most `GLIBC_2.14`; the bundled `libpython` needs `GLIBC_2.17`. That build
  loads and finalizes in Rack. A host-GCC build needs `GLIBC_2.35`, and
  `bin/rackpy-dev inspect --max-glibc 2.23` rejects it.
- **Start-up** (`bin/rackpy-dev startup`, 5 alternating runs with and without
  the skeleton, no entry module): plugin init took 11, 11, 12, 22 and 21 ms
  (median **12 ms**), measured from Rack's millisecond log timestamps. Rack's RSS
  after loading an empty patch was 21.1–21.3 MiB without RackPy and
  35.6–35.7 MiB with it (median delta **+14.2 MiB**; peak RSS +14.0 MiB). This
  is inside ADR 004's provisional budget (≤ 250 ms, ≤ 64 MB), but covers no
  bindings yet; RP-11 measures again. The machine is not controlled hardware.

## Known gaps

- `upstream/api-surface.json` records the libclang build that produced it, so
  only that build (clang 23.1.3 here) can reproduce it. CI's inventory job
  allows `pinned-libclang` and skips that one comparison visibly. Reproducing it
  in CI needs that exact LLVM, for example from the Arch Linux package archive.
- `bin/rack-sdk survey` compares a clang JSON AST dump and is not run in CI for
  the same reason.
- The plugin is rejected, and `destroy()` never runs, if `plugin.json` names a
  module that `init()` did not register. The skeleton names none; RP-10 and
  RP-16 must finalize correctly in that case.
- No macOS or Windows build, and no CI for them.
