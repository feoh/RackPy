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

Supported for development: **Linux x86_64** only. macOS and Windows builds do
not exist yet (RP-40, RP-41).

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
```

The first `bin/rackpy-dev build` downloads the Rack SDK (`bin/rack-sdk`) and
the bundled CPython 3.14 runtime (`packaging/inputs.json`), checks their
SHA-256 against the pins, and caches them under `$XDG_CACHE_HOME/rackpy`
(default `~/.cache/rackpy`). Nothing downloaded goes into the repository; the
build tree is `build/`, which git ignores. CMake checks `libRack.so` and every
header reachable from `rack.hpp` against
[tests/fixtures/sdk-provenance.json](tests/fixtures/sdk-provenance.json), so a
missing or different SDK is a configure error, not a warning.

## Commands

| Command | What it does |
| --- | --- |
| `bin/rackpy-dev build [--werror] [--toolchain rack-lin-x64] [--test-hooks]` | Configure, build, run the native unit tests. `--test-hooks` adds the RuntimeProbe module the runtime tests drive; never distribute that build |
| `bin/rackpy-dev bundle DEST [--runtime link\|copy]` | Install the build as the Rack plugin directory `DEST/RackPy` |
| `bin/rackpy-dev inspect DEST/RackPy [--max-glibc 2.23]` | Check exports, `NEEDED`, `RUNPATH`, Python references and the glibc floor |
| `bin/rackpy-dev audio-symbols FILE...` | Fail if an object references CPython (ADR 004's audio-path rule) |
| `bin/rackpy-dev lint`, `bin/rackpy-dev typecheck` | Ruff, and strict mypy including each `bin/` script |
| `bin/rack-sdk verify\|closure\|probe` | SDK pins ([compatibility](docs/compatibility.md#reproduce)) |
| `bin/rack-toolchain build\|check\|path` | The official Linux toolchain for distributable builds (about an hour, once) |
| `bin/planning-check` | `planning/` files agree with each other and form a DAG |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | The documentation site; conventions in [docs/contributing-docs.md](docs/contributing-docs.md) |

`bin/rackpy-dev --help` and each command's `--help` list every option.

## Tests

Every test has exactly one category marker, and collection fails if one is
missing, so no test can fall outside every CI job:

| Marker | Needs | Command |
| --- | --- | --- |
| `unit` | nothing but the project environment | `uv run pytest -m unit` |
| `native` | SDK, compiler, CMake | `uv run pytest -m "native and not host"` |
| `inventory` | clang and libclang | `uv run pytest -m inventory` |
| `host` | the pinned Rack Free 2.6.6 host (downloaded on first use); `gui` tests also need Xvfb | `uv run pytest -m "host and not dist"` |
| `dist` | the official toolchain from `bin/rack-toolchain build` | `uv run pytest -m dist` |
| `docs` | the docs toolchain (installed by `uv sync`); snippets marked `runs-in=rack` are also `host` | `uv run pytest -m "docs and not host"` |

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

Unit tests use a stand-in for the embedded `_rackpy_host` module and prove only
rackpy's own logic. Native tests link the SDK's `libRack.so` outside Rack.
Neither proves anything about loading in Rack; `tests/integration` does that.

## CI

[.github/workflows/ci.yml](.github/workflows/ci.yml) runs on pushes to `main`
and on pull requests, on `ubuntu-24.04`:

| Job | Runs |
| --- | --- |
| Planning data | `bin/planning-check`, `bin/callback-matrix check` |
| Hooks, typing and unit tests | `uv sync --frozen`, `prek run --all-files`, `pytest -m unit` |
| SDK pins, native build and audio-path guard | `bin/rack-sdk verify`, `closure`, `probe`; `pytest -m "native and not host"` |
| SDK inventory | `pytest -m inventory` with LLVM 21 from apt.llvm.org; see below |
| Documentation site | `sphinx-build -W` and `pytest -m "docs and not host"`; keeps the HTML |
| Documentation external links (weekly and on demand) | `sphinx-build -b linkcheck` |
| Rack host smoke tests | `pytest -m "host and not dist"`: the skeleton, the docs snippets that run in Rack, the RP-04 spike and the RP-06 probe in Rack Free 2.6.6 |
| Distributable build (pushes to `main` and on demand) | builds or restores the official toolchain, then `pytest -m dist` |

Each job keeps its JUnit report, the host jobs keep every Rack `log.txt`, and
the docs job keeps the built site, for 14 days. CI keeps no binaries:
distributing a build needs the RP-39 license manifest and owner approval.

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
