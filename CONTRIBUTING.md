# Contributing

Start with [the task graph](planning/README.md). RackPy has a build skeleton
(RP-08) but **no Rack API bindings yet**: the plugin it builds starts and stops
an embedded Python interpreter inside Rack and registers no modules. Nothing
here is an installable release.

## Before changing code

1. Read the architecture, coverage, and performance contracts.
2. Check `git status` and `git worktree list`; do not overwrite someone else's work.
3. Read the live Witan task and comments, verify its prerequisites, and claim it.
4. Use a separate worktree for branch experiments. Do not stash another person's work.
5. Keep a change bounded to its acceptance criteria.

## Set up a clean checkout

Supported for development: **Linux x86_64**, **macOS** on Apple silicon or
Intel (RP-40) and **Windows x64** (RP-41).

- On a Mac, Xcode's command line tools replace GCC and binutils. There is no
  Xvfb: windowed tests need a Mac whose GPU gives Rack an OpenGL window. The
  Linux-only checks (the official toolchain, the `LD_PRELOAD` audio-path
  guard, ELF inspection, the sanitizer and stress suites' Linux programs) skip
  with that reason.
- On Windows, build in MSYS2's MINGW64 shell with its GCC, CMake, Ninja and
  binutils (`pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,binutils}`), as Rack
  itself is built, with uv for Windows. The host tests also need 7-Zip's `7z`,
  to unpack Rack's installer, and Git for Windows' `bash.exe` for a few shell
  checks. Windowed tests need a GPU driver with OpenGL 2.0. The Linux-only
  checks skip as on a Mac; there are no ASan or TSan programs under MinGW.
- On Linux, `bin/rackpy-dev build --toolchain rack-win-x64` cross-builds with
  the official Windows toolchain. That is a build check only: a Wine run is
  not Windows evidence.

You need:

| Tool | Why |
| --- | --- |
| [uv](https://docs.astral.sh/uv/) ≥ 0.9.18 | Python environments, tools and the `bin/` scripts |
| CMake ≥ 3.25, Ninja, GCC with C++17 | the native build |
| binutils (`readelf`, `nm`, `objdump`), GNU Make, `jq` | bundle inspection; the SDK probe and spikes use the SDK's own makefiles |
| clang ≥ 21 with `libclang.so` | the SDK inventory (`-m inventory`); the pinned Python bindings need libclang 21 or newer |
| Xvfb (`xvfb-run`) and Mesa | windowed-Rack tests (`-m gui`) |
| Docker | only to build the official Rack toolchain (`bin/rack-toolchain build`) |
| Rack's runtime libraries: libGL, libX11, ALSA, JACK and PulseAudio client libraries | the native tests link `libRack.so`, and the host tests run Rack |

```bash
uv sync --frozen                 # project environment from uv.lock; fails if the lock is stale
bin/rackpy-dev build             # fetch pinned inputs, configure, build, run CTest
uv run pytest -m unit            # fast checks, no SDK needed
prek run --all-files             # hooks: ruff, strict mypy, JSON/YAML, planning data, callback matrix
prek install                     # once per clone: the same hooks before every commit and every push
```

`prek install` matters: CI's first job runs the same hooks and fails in under a
minute, but the rest of the run carries on for up to an hour. One line of
trailing whitespace pushed to `main` on 2026-11-07 failed seven runs in a row
that way, on `main` and on every branch cut from it. Worktrees share the
clone's hooks, so one install covers them all.

The first `bin/rackpy-dev build` downloads the Rack SDK (`bin/rack-sdk`) and
the bundled CPython 3.14 runtime (`packaging/inputs.json`), checks their
SHA-256 against the pins, and caches them under `$XDG_CACHE_HOME/rackpy`
(default `~/.cache/rackpy`). Nothing downloaded goes into the repository; the
build tree is `build/`, which git ignores. CMake checks `libRack.so` and every
header reachable from `rack.hpp` against
[tests/fixtures/sdk-provenance.json](tests/fixtures/sdk-provenance.json), so a
missing or different SDK is a configure error, not a warning.

The unit tests take about 30 seconds. For a fast real-Rack check, install
[ccache](https://ccache.dev/) and run the same subset as CI's fast lane; the first
run compiles everything, later runs reuse the compiler cache:

```bash
CCACHE_DIR=~/.cache/rackpy-ccache CCACHE_BASEDIR="$PWD" CCACHE_COMPILERCHECK=content \
CMAKE_CXX_COMPILER_LAUNCHER=ccache RACKPY_TEST_EVIDENCE= \
uv run pytest -m "host and not dist and not sanitizer and not stress" \
  tests/hardening/test_security.py \
  tests/examples/test_oscillator.py::test_the_plugin_loads_under_its_own_slug_and_keeps_its_patch
```

On one Linux machine (32 cores) this took 233 seconds with a cold cache and 51
seconds warm. It is a smoke test, not release evidence.

## Commands

| Command | What it does |
| --- | --- |
| `bin/rackpy-dev build [--werror] [--toolchain rack-lin-x64] [--test-hooks]` | Configure, build, run the native unit tests. `--test-hooks` adds the RuntimeProbe module the runtime tests drive; never distribute that build |
| `bin/rackpy-dev bundle DEST [--runtime link\|copy]` | Install the build as the Rack plugin directory `DEST/RackPy` |
| `bin/rackpy-dev inspect DEST/RackPy [--max-glibc 2.23]` | Check exports, `NEEDED`, `RUNPATH`, Python references and the glibc floor; on a Mac, the Mach-O load commands, install names and rpaths |
| `bin/rackpy-dev audio-symbols FILE...` | Fail if an object references CPython (ADR 004's audio-path rule) |
| `bin/rackpy-dev lint`, `bin/rackpy-dev typecheck` | Ruff, and strict mypy including each `bin/` script |
| `bin/rack-sdk verify\|closure\|probe` | SDK pins ([compatibility](docs/compatibility.md#reproduce)) |
| `bin/rack-toolchain build\|check\|path` | The official Linux toolchain for distributable builds (about an hour, once) |
| `bin/rackpy-release build\|verify\|introspect\|compare` | Release candidates for this computer's platform (Linux, the Mac's own architecture, or Windows x64) from a clean commit; install and run them on a clean container (Linux) or in fresh Rack user folders (a Mac, Windows); write what a candidate's RackPy exports inside Rack, for `bin/check_coverage.py check --introspection`; explain the differences between two builds ([Linux](docs/installation/linux.md), [macOS](docs/installation/macos.md), [Windows](docs/installation/windows.md)) |
| `bin/rackpy-plugin pack DIR` | A plugin directory as a reproducible `.vcvplugin` package |
| `bin/planning-check` | `planning/` files agree with each other and form a DAG |
| `bin/ci-failures [RUN] [--wait failure\|done]` | What has failed so far in a CI run, by platform, without waiting for the run to finish ([below](#when-a-run-fails)) |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | The documentation site; conventions in [docs/contributing-docs.md](docs/contributing-docs.md) |

`bin/rackpy-dev --help` and each command's `--help` list every option.

## Tests

Every test has exactly one category marker, and collection fails if one is
missing, so no test can fall outside every CI job:

| Marker | Needs | Command |
| --- | --- | --- |
| `unit` | nothing but the project environment | `uv run pytest -m unit` |
| `native` | SDK, compiler, CMake | `uv run pytest -m "native and not host and not sanitizer and not stress"` |
| `inventory` | clang and libclang | `uv run pytest -m inventory` |
| `host` | the pinned Rack Free 2.6.6 host (downloaded on first use); `gui` tests also need Xvfb | `uv run pytest -m "host and not dist and not sanitizer and not stress"` |
| `dist` | the official toolchain from `bin/rack-toolchain build` | `uv run pytest -m dist` |
| `docs` | the docs toolchain (installed by `uv sync`); snippets marked `runs-in=rack` are also `host` | `uv run pytest -m "docs and not host and not sanitizer and not stress"` |

The `sanitizer` and `stress` suites (RP-38) are opt-in: plain `uv run pytest`
leaves them out, through `addopts` in `pyproject.toml`. A `-m` of your own
replaces that one, so a selection repeats `not sanitizer and not stress`, as
above; run them with `-m sanitizer` or `-m stress`.

**A missing requirement fails the test.** A test that needs the SDK, Rack,
Xvfb, clang or the toolchain and cannot find it reports a setup error that
says how to fix it. To skip a requirement on purpose, name it:
`RACKPY_ALLOW_MISSING=xvfb-run uv run pytest`. Skips then appear in the
`-ra` summary with their reason. Platform gates (not Linux x86_64) are the only
automatic skips.

**No audio device.** The host tests never open an audio or MIDI device, so they
do not touch the machine's sound system. Rack runs headless (`Rack -h -u
<temporary user dir>`) or, for `gui` tests, with a real window on a virtual X
display (`xvfb-run`, Mesa software rendering), so nothing appears on the
desktop. Paths that only an audio driver thread exercises remain untested
(ADR 003). Each test uses a temporary Rack user directory, never your own.

**Do not run host tests in parallel** or while another Rack is starting: Rack
creates and removes `/tmp/Rack2` as it loads plugins.

**Suites that run inside Rack.** A directory named `in_rack` (for example
`tests/values/in_rack/`) holds ordinary pytest tests that import `rackpy.raw`
and so can only run in Rack. The outer run ignores them; a `host` test starts
each suite with `tests.support.run_pytest_in_rack`, which copies pytest into a
staged test bundle and runs the suite inside Rack's embedded interpreter
([ADR 009](docs/decisions/009-values-and-raw-conventions.md)).

Unit tests use a stand-in for the embedded `_rackpy_host` module and prove only
rackpy's own logic. Native tests link the SDK's `libRack.so` outside Rack.
Neither proves anything about loading in Rack; `tests/integration` does that.

## CI

CI has four sizes. Use the smallest that answers the question.

| Run | Start it with | What runs | Measured time, creation to last job |
| --- | --- | --- | --- |
| Pull request | opening or updating a pull request | Linux only: hooks, unit, native, docs, and a **fast host smoke** (the security suite and the oscillator plugin's load and patch-reopen test in a production build) | not measured |
| Fast dispatch | `gh workflow run ci.yml --ref BRANCH -f fast=true` | the pull-request checks, plus the same host smoke on macOS arm64/x64 and Windows | 4 to 11 minutes warm ([2026-11-05](planning/practical-release.md)) |
| Tests | `gh workflow run ci.yml --ref BRANCH` | every test suite on Linux, macOS arm64/x64 and Windows. No distributable build, no release candidates, no per-platform SDK inventory | 20 minutes with warm compiler caches (one run, [RP-56 validation](planning/rp-56-validation.md)) |
| Candidates | `gh workflow run ci.yml --ref BRANCH -f candidates=true`, and every push to `main` | the tests, plus the distributable build, the four release candidates and the per-platform SDK inventory | 41 and 43 minutes with warm compiler caches (two runs), 95 with cold ones (one run) ([RP-56 validation](planning/rp-56-validation.md)); the median was 59 before RP-56 |

Only a candidates run is release evidence ([release-evidence](release-evidence/README.md)):
`bin/release-evidence collect` records a tests run's platforms as having no
candidate, and the fast host steps write no per-test evidence at all. Until
RP-56 an ordinary dispatch always built the candidates, so every test
iteration on a branch waited for them; a branch needs them once, when its
tests already pass.

A newer dispatch on the same branch with the same inputs cancels the older
one, as a newer push to a pull request does. Candidates runs and pushes to
`main` are never cancelled by a later commit.

The host and native test jobs, in every size of run, build through
per-platform ccache stores; the fast and full suites of a platform share one
store. Compiler contents and build flags are part of the cache key, and each
job prints cache statistics. The cache covers only the tests' own builds (the
development, test-hooks and sanitizer trees). Release candidates and the
distributable build never use it, so candidate bytes are always compiled from
scratch. A job saves its store even when its tests fail, and the run's last
job deletes the branch's older stores, so the repository keeps one store per
platform per branch (about 2.5 GB a branch against GitHub's 10 GB limit). A
branch's first run starts from `main`'s store; with no store at all a platform
compiles for 40 to 70 minutes. `macos_tests` and `windows_tests` narrow a
tests or candidates run; do not combine them with `fast=true`. Runtime depends
on runner and build cache state; no completion time is promised.

The Ubuntu runner has no Python 3.14, so uv installs its own in every Linux
job. Those jobs keep it in the Actions cache (`cache-python` on `setup-uv`),
keyed on `pyproject.toml` and `uv.lock`: a run downloads it from GitHub only
after one of those files changes, or on a branch whose changed lock `main` has
not seen ([RP-61](planning/TASKS.md)).

### When a run fails

Of the 40 runs before RP-56, 23 failed
([measurements](planning/rp-56-validation.md)): seven only on one line of
trailing whitespace, nine with a test that failed on Linux or on several
platforms, four with tests failing on a single macOS or Windows platform, and
two outside the tests. So:

1. **Before pushing**, run what CI's first jobs run: the hooks (`prek install`
   does this for you) and, for a change to bindings or tests, the affected
   tests in a local Rack: `uv run pytest tests/<directory>`. A failure that
   reproduces on Linux costs seconds here and an hour in CI.
2. **Do not wait for a run to finish.** `bin/ci-failures --wait failure`
   returns at the first failed job, with the failed tests and the lines that
   look like the cause. Start it in the background (agents: an async subagent)
   and keep working.
3. **Read which platforms failed.** A test that failed on Linux, or on two or
   more platforms, is a bug in the code or the test: reproduce it locally
   before starting another run. A test that failed on one of macOS or Windows
   only is specific to that platform or intermittent. Rerun only the failed
   jobs with `gh run rerun RUN --failed`, not the whole workflow.
4. **An intermittent failure is a bug with a cause**, usually a test that
   waits on real time or on Rack's engine thread with a fixed budget. Record it
   on a Witan task with the run and job, and fix the wait. Do not add a retry
   and do not rerun until it passes without recording it.
5. Narrow a platform-specific investigation with `-f macos_tests=EXPR` or
   `-f windows_tests=EXPR` on a tests run: one suite job, a few minutes.

[.github/workflows/ci.yml](.github/workflows/ci.yml) runs on pushes to `main`
and on pull requests, on `ubuntu-24.04`:

| Job | Runs |
| --- | --- |
| Planning data | `bin/planning-check`, `bin/callback-matrix check` |
| Hooks, typing and unit tests | `uv sync --frozen`, `prek run --all-files`, `pytest -m unit` |
| SDK pins, native build and audio-path guard | `bin/rack-sdk verify`, `closure`, `probe`; `pytest -m "native and not host"` |
| SDK inventory | `pytest -m inventory` with LLVM 21 from apt.llvm.org; see below |
| Documentation site | `sphinx-build -W` and `pytest -m "docs and not host"`, split across ten jobs (a build that keeps the HTML, the ordinary tests, and one job per deliberately broken site); a unit test checks that every docs test runs in exactly one |
| Documentation external links (weekly, and tests and candidates dispatches) | `sphinx-build -b linkcheck` |
| Rack host smoke tests | Pull requests and fast dispatch: production-host security and oscillator load/patch smoke. Tests and candidates runs: `pytest -m "host and not dist"`, including docs snippets, the RP-04 spike and the RP-06 probe |
| Official Rack toolchain (candidates runs) | finds the toolchain in the cache, or builds and saves it (about an hour, once per toolchain pin), so the two jobs below can restore it and run side by side |
| Distributable build (candidates runs) | restores the official toolchain, then `pytest -m dist` |
| SDK inventory on mac-arm64, mac-x64 and win-x64 (candidates runs) | `bin/inventory_sdk.py platform-check`: each platform's SDK extracted natively and compared with the committed inventory |
| macOS arm64 and x86_64 (tests and candidates runs) | `macos-15` and `macos-15-intel`: the native build, CTest and `pytest -m "(unit or native or host) and not dist ..."`. The runners have no GPU, so a test-only plugin (`tests/fixtures/macos-software-gl`) gives Rack's window Apple's software OpenGL. `macos_tests` (a dispatch input) narrows the selection |
| Windows x64 (tests and candidates runs) | `windows-2025` with MSYS2's MINGW64 toolchain: the native build, CTest and `pytest -m "(unit or native or host) and not dist ..."`. Rack's window renders with the pinned Mesa llvmpipe (`bin/rack-sdk mesa`). `windows_tests` (a dispatch input) narrows the selection |
| macOS release candidates (candidates runs) | `bin/rackpy-release build` and `verify` on each architecture: Mach-O, signature and deployment-target checks, then every example installed into fresh Rack user folders with an empty environment; then `introspect` and `check_coverage.py check --introspection` against the candidate; keeps the manifest, checksums, introspection and logs |
| Windows x64 release candidate (candidates runs) | `bin/rackpy-release build` and `verify`: PE and DLL checks, then every example installed into fresh Rack user folders with Windows' own `PATH`; then `introspect` and `check_coverage.py check --introspection` against the candidate; keeps the manifest, checksums, introspection and logs |
| Linux release candidate (candidates runs) | `bin/rackpy-release build`, then `bin/rackpy-release verify` installs it on clean Ubuntu 20.04 and 16.04 containers with no Python and no network; then `introspect` and `check_coverage.py check --introspection` against the candidate; keeps the manifest, checksums, introspection, logs and screenshots |

Each job keeps its JUnit report, the host jobs keep every Rack `log.txt`, and
the docs job keeps the built site, for 14 days. The full test jobs also keep each
test's outcome (`RACKPY_TEST_EVIDENCE`), from which `bin/check_coverage.py
evidence` and `samples` judge each platform ([release evidence](release-evidence/README.md)). CI keeps no binaries:
distributing a build needs owner approval (RP-44).

**Known gap.** `upstream/api-surface.json` records the libclang build that
produced it (clang 23.1.3 on the planning machine). CI's LLVM 21 cannot
reproduce that record, so the inventory job allows `pinned-libclang` and skips
that one comparison, visibly; the fixture tests of the extractor still run.
Re-extracting needs the recorded libclang.

## Definition of done for implementation tasks

- The real supported SDK compiles the change, with no internal API access.
- Normal use, bad inputs, stale handles, thread violations, and failure paths have tests.
- Symbol mappings link to Python names, implementation, docs, and behavioral tests.
- New facade behavior has type hints, a narrative explanation, and runnable examples.
- C++ callbacks cannot leak exceptions into Rack; production DSP remains Python-free.
- Tests and docs use the same package that will be shipped, not a hidden mock.
- Relevant validation passes locally and in CI; unavailable platform tests remain
  explicit blockers, not successful checks.
- Upstream adaptations retain copyright, source revision, license, and changes.
- Record the exact commands and resulting artifacts in the Witan checkpoint.

## Releases and security

Release publication, signing credentials, official library submission, and
outbound licensing/trademark inquiries require owner approval. Please do not
put credentials, Rack Pro binaries, SDK archives, or personal patches into git.
Report security concerns privately to the repository owner rather than attaching
a malicious executable patch to a public issue.
