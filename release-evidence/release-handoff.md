# RackPy early Linux release: handoff for the owner's decision

**Status: published and post-upload verified.** On 2026-11-05 the owner
authorized a GitHub-only pre-release. [`v0.1.0.dev1`](https://github.com/feoh/RackPy/releases/tag/v0.1.0.dev1)
is live with all 15 authorized assets; hashes and the downloaded example plugins
were verified. No PyPI upload, VCV contact/submission or documentation-site
publication was made. This handoff is step 4 of
[the practical release plan](../planning/practical-release.md).

## Released scope

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
  this proposed Linux-only release.
- Coverage: 3,098 declarations are **implemented** on Linux; **none are marked
  verified**. The release must say bindings exist for the inventoried surface, with
  independent verification outstanding. It must not say "full coverage".
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
