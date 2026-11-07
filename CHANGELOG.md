# Changelog

## 1.0.0 — Linux x86_64 release (2026-11-07)

This GitHub release targets Rack Free 2.6.6 on Linux x86_64. The 1.0.0 version
starts the [facade compatibility policy](docs/reference/api-stability.md); it
is **not** a claim of complete Rack API coverage, physical audio deadlines,
performance certification or macOS/Windows support. Known API gaps remain
listed in [coverage](docs/coverage.md#known-release-gaps).

- Includes the runtime bundle, ten original example plugins, developer wheel,
  source archive, manifest and checksums. Each plugin carries its own Python;
  install only one RackPy plugin at a time.
- Native DSP runs without Python callbacks on the production audio path;
  [safety evidence and limitations](docs/performance-results.md) distinguish
  RackPy's path from host-owned synchronization.
- Distributed through GitHub Releases only; there is no PyPI upload or VCV
  Library submission. The public documentation site describes this limited scope.

## 0.1.0.dev1 — early Linux x86_64 preview

This pre-release targets Linux x86_64 with Rack Free 2.6.6 only. The runtime and
source artifact versions include `+g<commit>` to identify their exact source;
the wheel version is `0.1.0.dev1`. Rack plugin metadata in the `.vcvplugin`
examples continues to say `2.0.0`; that is Rack's plugin version field, not
RackPy's package version. This is not a claim of complete API coverage, broad
platform support, legal review, or performance certification.

- The GitHub release includes the runtime, all ten example plugins, developer
  wheel, source archive, manifest and SHA-256 checksums. Install one plugin at a
time; each contains its own Python runtime.
- The release candidate passed integrity and install/run/patch-reopen/windowed
  checks in clean Ubuntu 16.04 and 20.04 containers without system Python or
  network access. The exact commit, hashes and limits are recorded in the
  [`candidate audit`](release-evidence/candidate-audit.md).
- The owner also reported hearing the oscillator in a manual Rack Pro 2.6.6
  smoke; that is not a broad compatibility or performance claim.
- Added `rackpy.dialog` for selecting files and directories through Rack's native
  UI chooser, with cancellation, filters and save-name support. It is UI-thread
  only and returns a path; it never reads, executes or deserializes the selection.
  See [`docs/how-to/choose-files.md`](docs/how-to/choose-files.md).
- Documented Linux production audio-path/lifecycle safety evidence and its
  limitations in [`docs/performance-results.md`](docs/performance-results.md).
- Coverage remains incomplete and explicitly partial. Controlled performance
  measurement, physical audio-device deadlines and external audio/MIDI hardware
  remain unverified. The public distribution is GitHub-only: there is no PyPI
  upload, VCV Library submission or published documentation site.
