# Release evidence

What RackPy's full-surface claim rests on, platform by platform (RP-42). Everything here except this README and
`coverage/scope-review.md` is written by `bin/release-evidence collect RUN` from one finished CI run, and names
that run and its commit: the evidence is tied to an immutable ref, not to a working tree. `collect` and `promote`
require a completed run for the current commit and reject tracked working-tree changes. These checks do not lock
the checkout: do not edit or switch its files while collecting or promoting evidence.

The owner-approved practical partial-candidate audit is recorded separately in
[`candidate-audit.md`](candidate-audit.md). Its local-candidate manifests and
checksum lists are retained under `candidate-linux-x64-<commit>/`; the large
binary artifacts remain outside git. This does **not** satisfy or change the
full-surface release bar described below.

| File | What it records |
| --- | --- |
| `run.json` | The CI run, its URL, its commit, every job's conclusion, and the SHA-256 of the inventory, manifest and sample list the checks used |
| `SUMMARY.md` | One table: each platform's test outcomes, record evidence, sample parity and release bar |
| `coverage/<platform>.json` | That platform's record evidence (`bin/check_coverage.py evidence`), sample parity (`samples`), and the full-coverage release bar (`release`) against what its release candidate exports inside Rack, each with problem counts and the first 40 problems |
| `samples/<platform>.json` | Each official sample's outcome on that platform: its disposition's tests and how each ended |
| `coverage/scope-review.md` | The implementer's review of templates, macros, deprecated declarations, virtual overrides, platform branches and adapters for scope cheating (step 3), with what it found and fixed |

## What the checks mean

- **Record evidence.** Every bound record available on a platform cites a test that passed on that platform,
  and none that failed there. Test outcomes come from the CI test jobs (`RACKPY_TEST_EVIDENCE`), including each
  in-Rack test by name. A skipped test is not a pass.
- **Sample parity.** Every official tutorial, guide and template sample (`upstream/examples.json`) has a
  disposition whose tests all passed on that platform, or is out of scope with a reason (building Rack itself).
- **Release bar.** Every available declaration `verified` or `excluded_internal`, a real inventory, record
  evidence, and the candidate's introspection: `bin/rackpy-release introspect` runs one example plugin from the
  platform's release candidate, unchanged but for an added entry module, inside Rack Free, and records what its
  RackPy exports. The CI release-candidate jobs also run `bin/check_coverage.py check` against it.
- **Shared checker tests.** The synthetic coverage-checker mutation suites run once in the Linux unit job rather
  than on each OS. They are not binding or sample evidence, and a regression test forbids citing them as such.
  Collection reports a release-bar problem, and promotion refuses to run, if that shared job is missing or fails.
  All actual record and sample evidence remains specific to its target platform.

`verified` needs a reviewer (RP-42 step 6: a human or an explicitly authorized agent workflow). Until the owner
chooses one, no record is verified and the release bar stays closed on every platform. That is the expected
state, not a failure to hide.

## Open gates

Reported, not waived:

- No real GPU: CI draws Rack's window with software OpenGL (Mesa llvmpipe on Linux and Windows, Apple's software
  renderer on macOS).
- The audio-path guard (`LD_PRELOAD`) exists on Linux only; on macOS and Windows its tests are skipped, so the
  samples that cite them do not pass there (RP-43).
- `rack::system::runProcessDetached` on Windows asks UAC to elevate; it has no unattended test there.
- No physical audio or MIDI devices; signing and notarization; clean-machine and Gatekeeper checks beyond CI.

## Reproduce

```console
gh workflow run ci.yml -R feoh/rackpy --ref <branch>      # every platform's jobs, including the candidates
bin/release-evidence collect <run id>                       # once the run has finished
```

For faster diagnostic runs, `workflow_dispatch` accepts `macos_tests` and `windows_tests` pytest `-k`
expressions. Such runs may omit required platform evidence and are not substitutes for a full exact-candidate run
with both selectors empty. Each target job records its selection in its reports; collection blocks the release bar
and promotion refuses to proceed when an attestation is missing or nonempty. Runs created before these attestations
were added must be rerun to use this collector. CI keeps the underlying reports for 14 days and the
release-candidate reports for 30; the JSON here is the durable record.
