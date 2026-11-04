# Release-candidate audit (local preview, not a public release)

The owner approved an unpublished, local-only Linux preview at RackPy version
`0.1.0.dev1` on 2026-11-04. Public publication, tagging, uploading and VCV
submission remain unauthorized. The preview artifacts are staged outside the
repository; this audit records their exact source, manifest and checksums but
does not make them publicly available.

## Candidate built from this source

- Source commit: `6a62604a0dc81bd32dd0e16ce564813852847072` (clean detached worktree).
- Candidate platform built locally: Linux x86_64.
- Candidate metadata: [`manifest.json`](manifest.json),
  [`candidate-linux-x64/manifest.json`](candidate-linux-x64/manifest.json), and
  [`candidate-linux-x64/SHA256SUMS`](candidate-linux-x64/SHA256SUMS).
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
- The owner approved the proposed facade stability policy in
  [`docs/reference/api-stability.md`](../docs/reference/api-stability.md) on
  2026-11-04; it applies from the first release.
- Package publication, tagging, uploading artifacts, and contacting VCV remain
  gated on explicit owner approval. No signing credentials or public upload
  actions were used.

For this local preview, rebuild from the selected immutable commit, run the
checks in [the Linux preview instructions](../docs/installation/linux.md), and
install only on the owner's Linux machine. Before any public release, obtain
separate owner approval for its scope, artifact set, release notes, publication
destinations and signing/submission steps. Rebuild from the approved immutable
commit, rerun checks against those exact bytes on each target claimed, and
re-check coverage and safety statements.
