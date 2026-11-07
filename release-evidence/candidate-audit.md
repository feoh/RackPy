# RackPy 0.1.0.dev1 — Linux x86_64 preview release evidence

The first candidate was approved for local testing on 2026-11-04. On
2026-11-05 the owner separately authorized a GitHub-only pre-release with all
ten example plugins. No PyPI upload, VCV submission or docs-site publication is
in scope. The GitHub pre-release and its downloaded public bytes are recorded
below; the local candidates remain as history.

## Initial local candidate (historical)

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

## Later local Linux candidate (2026-11-05; superseded)

A second local-only build from clean commit
`1bc1093d24c28ba3169bd5eaa325252e433fe8c7` produced 13 artifacts as
`0.1.0.dev1+g1bc1093d2`, with its own manifest and `SHA256SUMS`, in
`/tmp/rackpy-preview-1bc1093/artifacts/`. It does not replace the exact
candidate or checksums above. Using Rack Free 2.6.6, the Ubuntu 16.04.7
and Ubuntu 20.04.6 container verifications passed artifact checksums, all ten
examples, patch reopen, windowed smokes, error/refusal cases, upgrade and
uninstall, with no system Python or network. Logs are in
`/tmp/rackpy-preview-1bc1093/verify-ubuntu-16.04/` and
`/tmp/rackpy-preview-1bc1093/verify-ubuntu-20.04/`. This is local Linux
container evidence, not a physical-device test, public release approval, or
validation of a subsequent source commit.

## Current local Linux candidate (2026-11-05)

A fresh, clean local build from commit
`7ed3627014c4cc312bff2840d63ce30ef2b10fcb` produced 13 artifacts as
`0.1.0.dev1+g7ed362701`, in
`/tmp/rackpy-preview-7ed3627/artifacts/`. The committed
[`candidate-linux-x64-7ed3627/manifest.json`](candidate-linux-x64-7ed3627/manifest.json)
and [`SHA256SUMS`](candidate-linux-x64-7ed3627/SHA256SUMS) identify the
artifacts; binaries remain outside git. The manifest records `dirty: false`,
`lin-x64`, and glibc floor 2.23.

`bin/rackpy-release verify` passed on Ubuntu 16.04.7 (glibc 2.23) and Ubuntu
20.04.6 (glibc 2.31), each in an offline container without system Python. Both
runs checked all artifact hashes, all ten plugin loads, module creation and
patch reopen, windowed Rack rendering, interpreter finalization, refusal/error
cases, upgrade and uninstall. Logs are under
`/tmp/rackpy-preview-7ed3627/verify-ubuntu-16.04/` and
`/tmp/rackpy-preview-7ed3627/verify-ubuntu-20.04/`. This remains local-only
preview evidence: it does not approve publication, establish physical-device
behavior or performance, or claim full API coverage. The later audit-document
commit does not change these candidate bytes; any eventual publication
candidate must be rebuilt from its final approved source commit.

The full CI workflow [run 41954139410](https://github.com/feoh/RackPy/actions/runs/41954139410)
completed successfully on earlier source commit
`7ed3627014c4cc312bff2840d63ce30ef2b10fcb`. All jobs passed, including Linux,
macOS arm64/x64 and Windows x64 native/host checks and candidate install/run
jobs. It is useful prior evidence, not validation of the chooser changes or the
final candidate, not platform parity, and not a reason to advertise macOS or
Windows in this Linux release.

## Local Linux candidate with file chooser (2026-11-05; pre-release-doc update)

A clean local build from commit
`78c42531af9bbfe92e0104045770ffed5c4fd946` produced 13 artifacts as
`0.1.0.dev1+g78c42531a`, in
`/tmp/rackpy-preview-78c4253/artifacts/`. The committed
[`candidate-linux-x64-78c4253/manifest.json`](candidate-linux-x64-78c4253/manifest.json)
and [`SHA256SUMS`](candidate-linux-x64-78c4253/SHA256SUMS) identify the
artifacts; binaries remain outside git. The manifest records `dirty: false`,
`lin-x64`, and glibc floor 2.23.

`bin/rackpy-release verify` passed on Ubuntu 16.04.7 (glibc 2.23) and Ubuntu
20.04.6 (glibc 2.31), in offline containers without system Python. Both runs
checked hashes, all ten plugins, patch reopen, windowed Rack rendering,
interpreter finalization, refusal/error cases, upgrade and uninstall. Logs are
under `/tmp/rackpy-preview-78c4253/verify-ubuntu-{16.04,20.04}/`. The new
`rackpy.dialog` chooser itself passed its focused in-Rack interposer test on
this source branch: open/save/directory actions, filters, default save name,
selection and cancellation. This candidate is still local-only evidence; it
does not approve publication or establish physical-device behavior, performance,
full API coverage, or macOS/Windows support. The follow-up documentation commit
records this candidate but does not change its bytes; any public candidate must
be rebuilt from its final approved source commit.

## Published GitHub pre-release (2026-11-05)

A fresh build from clean source commit
`c5269274f08191e453fe478fcdfb067b0fba7180` produced 13 build artifacts (runtime,
ten plugins, wheel and source) plus `manifest.json` and `SHA256SUMS` (15 files,
510 MB total). The manifest records `dirty: false`, `lin-x64`, Rack Free 2.6.6
and glibc floor 2.23. The preserved
[`candidate-linux-x64-c526927/manifest.json`](candidate-linux-x64-c526927/manifest.json)
and [`SHA256SUMS`](candidate-linux-x64-c526927/SHA256SUMS) identify the build
artifacts and manifest; the checksum file is the final attached file. All 15
files are published as the pre-release [`v0.1.0.dev1`](https://github.com/feoh/RackPy/releases/tag/v0.1.0.dev1),
targeting this exact source commit. Binaries are not committed to git.

After download from GitHub, `sha256sum -c SHA256SUMS` verified every listed
build artifact and manifest. `bin/rackpy-release verify` passed against the
downloaded public assets on Ubuntu 20.04.6 in an isolated container without
system Python and with networking disabled. It checked all ten plugins, patch
reopen, windowed Rack rendering, interpreter finalization, refusal/error cases,
upgrade and uninstall. The public-download run is logged at
`/tmp/rackpy-release-c526927/verify-public-ubuntu-20.04/`; the pre-upload checks
also passed on Ubuntu 16.04.7 (glibc 2.23). On source commit `c526927`,
`prek run --all-files`, warning-free Sphinx HTML build, and release/docs
integration tests (21 passed) passed.

Full GitHub Actions run
[42022956074](https://github.com/feoh/RackPy/actions/runs/42022956074) completed
successfully on chooser-bearing source commit `78c42531af9bbfe92e0104045770ffed5c4fd946`.
The tagged source `c526927` adds release-facing documentation and was validated
locally; no cross-platform support is implied by this Linux release.

## Earlier cross-platform CI evidence

GitHub Actions run
[41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030) ran
candidate install/run checks on Linux x86_64, macOS x86_64/arm64 and Windows
x86_64 for earlier source commit
`16ecf405932b857197406673bec1d16ff09115c5`. That is useful prior evidence, not
validation of this exact commit or its artifacts. The run's optional external
link checker failed on GitHub HTTP 429; required documentation and candidate,
native and host jobs passed.

## Newer CI-only candidate (2026-11-06)

[CI run 42263701004](https://github.com/feoh/RackPy/actions/runs/42263701004)
completed successfully on source commit `449cbaec87865a13e175dbc44d1c62b07b296f7e`.
It built and exercised separate Linux x86_64, macOS x86_64/arm64 and Windows
x86_64 candidates. These are **not the published `v0.1.0.dev1` assets**, and the
workflow retained evidence, not the candidate binaries. Manifests, checksums and
verification summaries downloaded from that run are preserved in
[`candidate-ci-449cbae/`](candidate-ci-449cbae/) (summary trailing whitespace
normalized by repository hooks); hashes identify CI's artifact
bytes, which were not independently downloaded or rehashed in this audit. The
manifest consistency checker also passed locally with each of the four
candidate introspection files downloaded from that run; that is an export
consistency check, not independent proof of every binding's semantics.

Linux Ubuntu 16.04 and 20.04 checks reported all ten examples loading, patch
reopen, windowed rendering, upgrade and uninstall. macOS and Windows reports
also record ten examples and patch reopen, but explicitly report their candidate
windowed checks **not run**: those runners could not create an OpenGL window.
A green job does not turn those OPEN checks into passes. The Linux dynamic
audio-path guard does not establish equivalent macOS/Windows instrumentation.
This evidence does not extend the published Linux-only support statement or
prove physical-device performance, full API coverage, or official-sample parity.

## Published GitHub release v1.0.0 (2026-11-07)

The owner authorized publication on 2026-11-06. The published Linux x86_64
release targets Rack Free 2.6.6, glibc 2.23 or newer, and intentionally does
not claim complete API coverage, measured performance, physical audio deadlines,
macOS/Windows support, signing, PyPI or VCV Library distribution. Its exact
source tag is `v1.0.0` at
`e329b109a595d881527aebd8aee5a86a4fd96a33`. Exact-SHA CI run
[42284808506](https://github.com/feoh/RackPy/actions/runs/42284808506) completed
successfully. Local build exit code was 0; the pre-upload bytes passed
`SHA256SUMS` and verification in clean Ubuntu 16.04.7 and 20.04.6. Candidate
manifest and checksums are committed in
[`candidate-linux-x64-e329b10/`](candidate-linux-x64-e329b10/); binaries stay
outside git. Post-upload public-download checks are recorded in
[`release-handoff.md`](release-handoff.md).

## Scope and remaining gates

- This is an **early partial preview**, not complete supported-public-API
  coverage. The coverage report remains authoritative; no full-surface claim is
  made. GitHub-only publication is authorized; no PyPI or VCV activity is in
  scope.
- The supported preview target is Linux x86_64 with Rack Free 2.6.6. CI evidence
  for other platforms does not establish platform parity or extend this release's
  support statement.
- No controlled performance result or physical audio deadline test is claimed.
  Linux audio-path safety evidence and its limits are in
  [`docs/performance-results.md`](../docs/performance-results.md).
- The owner approved the proposed facade policy in
  [`docs/reference/api-stability.md`](../docs/reference/api-stability.md); it
  applies from the first public release.
- Artifacts are unsigned. No signing/notarization, external legal review, or
  physical audio/MIDI hardware test is claimed. The hosted docs describe the
  Linux-only release; no broader platform or distribution channel is implied.
- Post-upload verification is complete: all 15 public files were downloaded,
  all listed checksums matched, and the ten example plugins passed the release
  verification suite from the downloaded public assets on Ubuntu 20.04.6.
