# Release-candidate audit (not a release)

No RackPy release has been published or approved. The binaries from this audit
were staged outside the repository and are not included here. The manifest and
checksums below preserve the identity of the Linux candidate; they do not make
the artifacts publicly available.

## Candidate built from this source

- Source commit: `6a62604a0dc81bd32dd0e16ce564813852847072` (clean detached worktree).
- Candidate platform built locally: Linux x86_64.
- Candidate metadata: [`candidate-linux-x64/manifest.json`](candidate-linux-x64/manifest.json)
  and [`candidate-linux-x64/SHA256SUMS`](candidate-linux-x64/SHA256SUMS).
- Rebuild command from a clean checkout: `bin/rackpy-release build --dest <outside-repo-directory>`.
- The 13 staged artifacts (runtime, 10 example plugins, developer wheel, source
  archive) matched `SHA256SUMS`. Rack's manifest records each archive's size,
  content digest and artifact SHA-256. The staged candidate occupied 510 MB.
- `bin/rackpy-release verify` installed the candidate in a clean Ubuntu 20.04
  container with no system Python and `--network none`. All ten example plugins
  loaded, patches reopened, plugin upgrade/uninstall behavior passed, the
  windowed Rack smoke cases passed, and no Python/module errors were reported.
  The candidate manifest and every checksum were validated by the verifier.

The GitHub Actions run
[41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030) validated
install/run and in-candidate manifest checks on Linux x86_64, macOS x86_64/arm64,
and Windows x86_64 for source commit `16ecf405932b857197406673bec1d16ff09115c5`.
The current source commit adds documentation/evidence after that commit. Thus
cross-platform evidence exists for that earlier candidate; the exact 6a62604
artifact set was locally built and validated only on Linux. The run's optional
external-linkcheck failed on GitHub HTTP 429; required documentation build and
all platform candidate/native/host jobs passed.

## Claims and decisions still open

- This is a **partial, unreleased development candidate**, not a complete
  supported-public-API implementation. The coverage report remains the source
  of truth; no full-surface claim is made.
- The Linux candidate smoke tests do not prove a physical audio-device deadline
  budget or a controlled performance result; see
  [`../docs/performance-results.md`](../docs/performance-results.md).
- Exact-current-commit macOS/Windows artifacts and tests have not been produced.
- No PyPI name reservation has been verified. Do not upload an empty or
  nonfunctional package.
- The proposed facade stability policy remains unapproved. Do not present it as
  a compatibility promise.
- Package publication, tagging, uploading artifacts, and contacting VCV remain
  gated on explicit owner approval. No signing credentials or public upload
  actions were used.

Before any public release, the owner must choose a preview version and approve
its scope, artifact set, release notes, publication destinations, and any
signing/submission steps. Rebuild from the approved immutable commit, rerun the
checks against those exact bytes on each target claimed, and re-check coverage
and safety statements.
