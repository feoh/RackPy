# RackPy early Linux release: handoff for the owner's decision

**Status: GitHub publication authorized, but not yet tagged or uploaded.** On
2026-11-05 the owner authorized a GitHub pre-release only; the remaining work is
to finish the exact-ref checks, rebuild and verify the final candidate, then
publish and verify its public bytes. No PyPI upload, VCV contact/submission or
documentation-site publication is authorized. This handoff is step 4 of
[the practical release plan](../planning/practical-release.md).

## What is proposed

An **early, partial, Linux x86_64 release for Rack 2.6.6**, labelled a preview
(a pre-release, not a stable one). It lets a Python developer build a Rack plugin
with the documented tools, install it, and hear and save it. It does not claim
complete API coverage, macOS or Windows support, real-time guarantees or measured
performance.

## Candidate on hand (local only)

| Item | Value |
| --- | --- |
| Source commit | `78c42531af9bbfe92e0104045770ffed5c4fd946`, clean checkout (`dirty: false`) |
| Version string | `0.1.0.dev1+g78c42531a` (the owner-selected `0.1.0.dev1` preview version) |
| Platform | `lin-x64`, glibc floor 2.23, Rack Free 2.6.6 |
| Location | `/tmp/rackpy-preview-78c4253/artifacts/`, outside git |
| Artifacts | 13: runtime, ten example `.vcvplugin` packages, developer wheel, source archive, manifest and `SHA256SUMS` (about 510 MiB total) |

**This is not yet the candidate for publication.** These artifacts were verified
on Ubuntu 16.04 and 20.04 and are tied to commit `78c4253`; the handoff/audit
and final publication selection are still being prepared. Rebuild from the final
approved clean source commit and verify again before upload. The checksums of
this local build are in its own `SHA256SUMS`; a final candidate may have
different ones, and only those may be considered for publication.

## Evidence, labelled

Verified, with the command or log:

- On the exact local candidate above, `bin/rackpy-release verify` passed in
  clean **Ubuntu 16.04.7** and **Ubuntu 20.04.6** containers, with no system
  Python and Docker networking disabled: checksums, all ten example plugins,
  patch reopen, windowed module-browser smokes, refusal/error cases, upgrade
  and uninstall. Logs are under `/tmp/rackpy-preview-78c4253/verify-*`;
  committed candidate metadata is in
  [`candidate-linux-x64-78c4253/`](candidate-linux-x64-78c4253/).
- Documented first-plugin workflow (`rackpy-plugin create`, `add-module`, `build`,
  `pack`) produced a module that Rack Free 2.6.6 loaded and whose patch reopened
  ([candidate audit](candidate-audit.md)). That ran against the earlier `52526e83`
  runtime, not these bytes.
- The full unit suite passed 266 tests; the focused Rack-native dialog
  interposer test passed in actual Rack. Full CI run
  [42022956074](https://github.com/feoh/RackPy/actions/runs/42022956074) is
  running on the candidate source commit; do not treat it as green until its
  final status is rechecked.
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

## Known limitations to state in the release notes

- Install **one** RackPy plugin at a time; each carries its own Python runtime.
- Each example is about 47 MB because it bundles Python.
- Coverage is partial in the sense above; the long-term full-surface goal stays open.
- Linux x86_64 and Rack 2.6.6 only.

## Confirmed publication scope

- **Destination:** GitHub Releases on `feoh/RackPy` only; create a pre-release
  tagged `v0.1.0.dev1`.
- **Version:** `0.1.0.dev1`; runtime and source names include `+g<commit>`, and
  the developer wheel uses the plain PEP 440 version.
- **Assets:** all ten Linux x86_64 example `.vcvplugin` packages, runtime bundle,
  developer wheel, source archive, `manifest.json` and `SHA256SUMS` (about 510 MiB).
- **Not in scope:** no PyPI upload, VCV contact/Library submission or published
  documentation site. The Linux artifacts are unsigned; the release notes will
  disclose that. No legal review, physical-device audio deadline or performance
  certification is claimed.

## Publication sequence

After the full workflow on the chooser-bearing source commit completes, build a
fresh candidate from the final clean release-document commit. Run `verify` on
Ubuntu 16.04 and 20.04 and retain that exact manifest and checksums. Create the
GitHub pre-release with only the authorized assets, download every public asset,
compare its checksum with the approved manifest, install a published example
from those bytes in a clean container, and record the result. No PyPI, VCV or
documentation-site publication is part of this release. RP-44 remains open until
the public bytes are verified.
