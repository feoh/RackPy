# RackPy early Linux release: handoff for the owner's decision

**Status: a proposal. Nothing is published, tagged, uploaded or sent to VCV, and
nothing here is approval to do so.** It is step 4 of
[the practical release plan](../planning/practical-release.md). Step 5, publication,
needs your explicit answers to the decisions below.

## What is proposed

An **early, partial, Linux x86_64 release for Rack 2.6.6**, labelled a preview
(a pre-release, not a stable one). It lets a Python developer build a Rack plugin
with the documented tools, install it, and hear and save it. It does not claim
complete API coverage, macOS or Windows support, real-time guarantees or measured
performance.

## Candidate on hand (local only)

| Item | Value |
| --- | --- |
| Source commit | `7ed3627014c4cc312bff2840d63ce30ef2b10fcb`, clean checkout (`dirty: false`) |
| Version string | `0.1.0.dev1+g7ed362701` (the owner-selected `0.1.0.dev1` local-preview version) |
| Platform | `lin-x64`, glibc floor 2.23, Rack Free 2.6.6 |
| Location | `/tmp/rackpy-preview-7ed3627/artifacts/`, outside git |
| Artifacts | 13: runtime bundle (about 47 MB), ten example `.vcvplugin` packages (about 47 MB each), developer wheel (147 KB), source archive (2.4 MB), plus `manifest.json` and `SHA256SUMS` |

**This is not a release candidate for publication.** The artifacts were verified
on Ubuntu 16.04 and 20.04 and are tied to commit `7ed3627`; this handoff and audit
were updated afterward. Any candidate proposed for publication must be rebuilt
from the final approved clean source commit and verified again. The checksums of
this local build are in its own `SHA256SUMS`; a rebuilt candidate will have
different ones, and only those may be considered for publication.

## Evidence, labelled

Verified, with the command or log:

- On the exact candidate above, `bin/rackpy-release verify` passed in clean
  **Ubuntu 16.04.7** and **Ubuntu 20.04.6** containers, with no system Python
  and Docker networking disabled: checksums, all ten example plugins, patch
  reopen, windowed module-browser smokes, refusal/error cases, upgrade and
  uninstall. Logs are under `/tmp/rackpy-preview-7ed3627/verify-*`; committed
  candidate metadata is in [`candidate-linux-x64-7ed3627/`](candidate-linux-x64-7ed3627/).
- Documented first-plugin workflow (`rackpy-plugin create`, `add-module`, `build`,
  `pack`) produced a module that Rack Free 2.6.6 loaded and whose patch reopened
  ([candidate audit](candidate-audit.md)). That ran against the earlier `52526e83`
  runtime, not these bytes.
- On the candidate source, the sample-disposition tests passed (76) and the
  template unit tests passed (7). Full CI run
  [41954139410](https://github.com/feoh/RackPy/actions/runs/41954139410)
  completed successfully on the exact candidate source commit, including the
  Linux unit/native/host checks and all platform candidate workflows.
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

## Decisions I need from you

1. **Go or no go** for a first public pre-release at all, or keep it local for now.
2. **Version.** Keep `0.1.0.dev1` (the runtime and source artifacts add
   `+g<commit>`; the wheel is plain `0.1.0.dev1`), or use `0.1.0a1`, my earlier
   recommendation.
3. **GitHub.** Create a pre-release on `feoh/RackPy` with a tag you name (suggested
   `v0.1.0.dev1`), attaching the artifacts and `SHA256SUMS`?
4. **Which artifacts.** All ten 47 MB examples, or only the runtime, `Oscillator` and
   `Template` examples plus the wheel and source?
5. **PyPI.** The wheel is a developer package (typing, offline graph tests) that runs
   nothing in Rack. PyPI's policy treats empty or non-functional packages as name
   squatting. Publish it, or hold the `rackpy` name until a functional distribution?
6. **VCV.** Do not contact VCV or submit to the VCV Library at this stage. Confirm.
7. **Signing.** Linux packages are unsigned and need no notarization. Confirm that is
   acceptable.
8. **Docs.** Publish the documentation site, or leave it in the repository?

## What happens after approval

Rebuild from the final clean commit, run `verify` on 16.04 and 20.04, update
`manifest.json`, `candidate-linux-x64/` and the audit, publish only the approved
items, download the public bytes, compare checksums, install one example from them in
a clean container, and record the result. RP-44 closes only when that delivered
outcome is verified.
