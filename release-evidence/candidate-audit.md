# RackPy 0.1.0.dev1 — local Linux preview audit (not a public release)

The owner approved this Linux x86_64 preview for local testing only on
2026-11-04. Nothing is published, tagged or approved for redistribution. The
candidate binaries live outside git at
`/tmp/rackpy-preview-52526e8/artifacts/`; this repository retains only their
manifest and checksums.

## Exact candidate

- RackPy package version: `0.1.0.dev1`; the runtime and source artifact version
  adds `+g52526e83c` to identify the source. The `.vcvplugin` files say `2.0.0`,
  which is Rack's plugin version field, not RackPy's package version.
- Source commit: `52526e83c0c0bfc74d223e46ea78319450336766` (clean checkout,
  `dirty: false` in the manifest).
- Platform: Linux x86_64 (`lin-x64`), Rack Free 2.6.6, glibc floor 2.23.
- Artifact manifest: [`manifest.json`](manifest.json), also retained as
  [`candidate-linux-x64/manifest.json`](candidate-linux-x64/manifest.json).
- SHA-256 list: [`candidate-linux-x64/SHA256SUMS`](candidate-linux-x64/SHA256SUMS).
  All 13 artifacts and the manifest matched it after the build.
- Artifact folder: 510 MB, outside the repository; no binaries are committed.
  It contains the runtime bundle, ten example `.vcvplugin` packages, developer
  wheel, source archive, manifest and checksums.

Rebuild the exact commit and repeat the clean-container install/run suite using
the [Linux preview instructions](../docs/installation/linux.md). On the build
machine, this candidate was verified with:

```console
bin/rackpy-release verify /tmp/rackpy-preview-52526e8/artifacts \
  --rack /home/feoh/.cache/rackpy/host/x/lin-x64/Rack2Free \
  --logs /tmp/rackpy-preview-52526e8/verify-ubuntu-20.04 --image ubuntu-20.04
```

The clean Ubuntu 20.04.6 container had no system Python and no network. All
checks passed: checksums, all ten plugin loads, module creation and patch
reopen, windowed module-browser smokes, interpreter finalization, refusal and
error cases, upgrade, and uninstall. The full output is in
`/tmp/rackpy-preview-52526e8/verify-ubuntu-20.04/summary.txt` on this machine.
On 2026-11-04, the same candidate also passed `bin/rackpy-release verify`
with `--image ubuntu-16.04` in Ubuntu 16.04.7 (glibc 2.23), again with no
system Python or network. Checksums, all ten examples, patch reopen, windowed
smokes, error cases, upgrade and uninstall passed. The logs are at
`/tmp/rackpy-preview-52526e8/verify-ubuntu-16.04/` on this machine.

The owner subsequently reported manually installing the oscillator in Rack Pro
2.6.6 and hearing its
tone through headphones; that is one owner-reported smoke, not broad Rack Pro
support or a deadline measurement. The container test does not exercise
physical audio/MIDI devices, audio deadlines, or hardware GPU drivers.

## First plugin and packaging smoke (2026-11-04)

On Linux x86_64, using the runtime bundle from this candidate and the tools at
commit `e3b9327`, the documented `rackpy-plugin create`, `add-module`, `build`
and `pack` workflow created a new `MyPlugin` with a module from the oscillator
panel. Rack Free 2.6.6 extracted the resulting package in an isolated temporary
user folder, loaded `MyPlugin 2.0.0`, and retained a patch containing
`MyModule` on a second headless launch; the extracted plugin was then removed.
The temporary test files were deleted. This checks that this particular
candidate/runtime and tool version work together; it does not validate newer
candidate binaries, interactive rendering, generated audio, upgrade behavior,
or another operating system.

## Earlier cross-platform CI evidence

GitHub Actions run
[41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030) ran
candidate install/run checks on Linux x86_64, macOS x86_64/arm64 and Windows
x86_64 for earlier source commit
`16ecf405932b857197406673bec1d16ff09115c5`. That is useful prior evidence, not
validation of this exact commit or its artifacts. The run's optional external
link checker failed on GitHub HTTP 429; required documentation and candidate,
native and host jobs passed.

## Scope and remaining gates

- This is a **local partial preview**, not a complete supported-public-API
  implementation and not a public platform-support statement. The coverage
  report remains authoritative; no full-surface claim is made.
- No controlled performance result or physical audio deadline test is claimed.
  Linux audio-path safety evidence and its limits are in
  [`docs/performance-results.md`](../docs/performance-results.md).
- The owner approved the proposed facade policy in
  [`docs/reference/api-stability.md`](../docs/reference/api-stability.md); it
  applies from the first public release.
- Exact-current-commit macOS/Windows packages are not part of this Linux-only
  preview. No PyPI name reservation is verified; do not upload a placeholder
  or this private developer wheel.
- Public publication, tagging, PyPI/GitHub upload, VCV submission, signing and
  notarization still require separate explicit owner approval. Local testing
  approval does not grant any of those permissions.
