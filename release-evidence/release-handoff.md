# RackPy Linux releases: publication record

**Current release: v1.0.0, published and post-upload verified on 2026-11-07.**
The GitHub-only release targets Linux x86_64 and Rack Free 2.6.6. Its exact
source, assets, hashes, limitations and post-upload checks are recorded below.
The earlier [`v0.1.0.dev1`](https://github.com/feoh/RackPy/releases/tag/v0.1.0.dev1)
remains a separate preview; its handoff is retained here as history.

## v1.0.0 — Linux x86_64 (2026-11-07)

| Item | Value |
| --- | --- |
| Source commit | `e329b109a595d881527aebd8aee5a86a4fd96a33` (tag `v1.0.0`) |
| Package version | `1.0.0` (runtime/source identify `+ge329b109a`) |
| Platform | Linux x86_64 (`lin-x64`), glibc 2.23 floor, Rack Free 2.6.6 |
| Release | [GitHub release v1.0.0](https://github.com/feoh/RackPy/releases/tag/v1.0.0) |
| Assets | 13 build artifacts, `manifest.json`, `SHA256SUMS` (15 files) |
| Retained metadata | [`candidate-linux-x64-e329b10/`](candidate-linux-x64-e329b10/) |

The owner authorized this GitHub-only release on 2026-11-06 (epic decision in
Witan). The candidate build exited 0; exact-SHA CI run
[42284808506](https://github.com/feoh/RackPy/actions/runs/42284808506) completed
successfully. The local artifacts passed `SHA256SUMS` and clean Ubuntu 16.04.7
and 20.04.6 `bin/rackpy-release verify` runs before publication. The public
assets were downloaded after publication, checksum-verified and tested again
on clean Ubuntu 20.04.6; details are recorded below.

The release is intentionally partial: it does not claim complete API coverage,
controlled performance, physical audio-device deadlines, macOS or Windows
support, or signed binaries. It is GitHub-only; there is no PyPI upload or VCV
Library submission/contact.

### Post-upload public-byte verification

`gh release download v1.0.0 -R feoh/RackPy` downloaded all 15 public files.
`sha256sum -c SHA256SUMS` passed for all 13 build artifacts and the manifest.
The public download then passed
`bin/rackpy-release verify` in a clean Ubuntu 20.04.6 container with no system
Python and no network: ten example plugin loads, patch save/reopen, windowed
Rack rendering, refusal/error cases, upgrade and uninstall. The complete
summary and hash-check report are retained in
[`verify-public-e329b10/`](verify-public-e329b10/). The published SHA-256 of
`manifest.json` is `51e228fdd90abc7f286643da7b1c44e03b71176c8ec7008d27a12b47ba78440d`;
`SHA256SUMS` is `ee7d4e7facd9137f54daafbd2e5b27598c0f348470238c6720e974b759c6e636`.

## v0.1.0.dev1 — early Linux x86_64 preview (2026-11-05)

An **early, partial, Linux x86_64 release for Rack 2.6.6**, labelled a preview
(a pre-release, not a stable one). It lets a Python developer build a Rack plugin
with the documented tools, install it, and hear and save it. It does not claim
complete API coverage, macOS or Windows support, real-time guarantees or measured
performance.

## Published candidate

| Item | Value |
| --- | --- |
| Source commit | `c5269274f08191e453fe478fcdfb067b0fba7180`, clean checkout (`dirty: false`) |
| Version string | `0.1.0.dev1+gc5269274f` (owner-authorized `0.1.0.dev1` preview version) |
| Platform | `lin-x64`, glibc floor 2.23, Rack Free 2.6.6 |
| Availability | [GitHub pre-release](https://github.com/feoh/RackPy/releases/tag/v0.1.0.dev1) |
| Assets | 15 files: 13 build artifacts (runtime, ten plugins, wheel and source), plus manifest and `SHA256SUMS` (510 MB total) |

The release was built from this clean source commit and verified on Ubuntu
16.04 and 20.04 before upload. Candidate checksums are preserved in
[`candidate-linux-x64-c526927/`](candidate-linux-x64-c526927/); the uploaded
copies were downloaded and verified again after publication.

## Evidence, labelled

Verified, with the command or log:

- On the exact local candidate above, `bin/rackpy-release verify` passed in
  clean **Ubuntu 16.04.7** and **Ubuntu 20.04.6** containers, with no system
  Python and Docker networking disabled: all 13 build artifacts and the manifest
  matched `SHA256SUMS`; ten example plugins, patch reopen, windowed Rack
  rendering, refusal/error cases, upgrade
  and uninstall. Logs are under `/tmp/rackpy-release-c526927/verify-*`;
  committed candidate metadata is in
  [`candidate-linux-x64-c526927/`](candidate-linux-x64-c526927/).
- Documented first-plugin workflow (`rackpy-plugin create`, `add-module`, `build`,
  `pack`) produced a module that Rack Free 2.6.6 loaded and whose patch reopened
  ([candidate audit](candidate-audit.md)). That ran against the earlier `52526e83`
  runtime, not these bytes.
- The full unit suite passed 266 tests and the focused Rack-native dialog
  interposer test passed in actual Rack on the chooser-bearing source. GitHub
  Actions run [42022956074](https://github.com/feoh/RackPy/actions/runs/42022956074)
  completed successfully on code commit `78c4253`; the final release candidate
  `c526927` adds release-facing documentation, which passed local strict Sphinx
  and docs integration tests.
- Licence texts and third-party notices ship inside each package; the manifest lists
  17 third-party components.
- You heard the earlier oscillator candidate through headphones in Rack Pro 2.6.6.
  One manual smoke on one machine, not a Pro compatibility claim.

Not verified:

- The local Linux artifact checksums do not describe separately built macOS or
  Windows bytes. Full CI did build and exercise candidates on macOS arm64/x64
  and Windows x64 at source commit `7ed3627`, but those platforms are not part of
  this published Linux-only release.
- Coverage: 3,098 declarations are **implemented** on Linux; **none are marked
  verified**. The release notes state that bindings exist for the inventoried
  surface, with independent verification outstanding; they do not claim full
  coverage.
- Performance and physical-device audio deadlines. No controlled benchmark exists.
- Physical audio or MIDI hardware and hardware GPU drivers.
- A legal review of the licence mix (the plugin bundles CPython and RackPy under
  GPL-3.0-or-later). The documentation gives no legal advice.

## Limitations disclosed in the release notes

- Install **one** RackPy plugin at a time; each carries its own Python runtime.
- Each example is about 47 MB because it bundles Python.
- Coverage is partial in the sense above; the long-term full-surface goal stays open.
- Linux x86_64 and Rack 2.6.6 only.

## Published scope

- **Destination:** GitHub Releases on `feoh/RackPy`; published pre-release
  [`v0.1.0.dev1`](https://github.com/feoh/RackPy/releases/tag/v0.1.0.dev1).
- **Version:** `0.1.0.dev1`; runtime and source names include `+g<commit>`, and
  the developer wheel uses the plain PEP 440 version.
- **Assets:** all ten Linux x86_64 example `.vcvplugin` packages, runtime bundle,
  developer wheel, source archive, `manifest.json` and `SHA256SUMS` (about 510 MiB).
- **Not in scope:** no PyPI upload, VCV contact/Library submission or published
  documentation site. The Linux artifacts are unsigned; this is disclosed in
  the release notes. No legal review, physical-device audio deadline or
  performance certification is claimed.

## Post-publication verification (completed)

Full GitHub Actions run 42022956074 finished successfully. The fresh candidate
was built from clean commit `c526927`, verified on Ubuntu 16.04 and 20.04, and
published with all authorized assets. All 15 public files were downloaded;
`sha256sum -c SHA256SUMS` passed, then `bin/rackpy-release verify` passed using
the public download on Ubuntu 20.04.6 with no system Python and networking
disabled. No PyPI, VCV or documentation-site publication was made. Evidence and
checksums are committed; only final live Witan task closure remains.
