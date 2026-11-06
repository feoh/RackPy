# Release evidence

What a future RackPy full-surface claim must rest on, platform by platform (RP-42). The coverage, sample and
run reports listed below are written by `bin/release-evidence collect RUN` from one finished CI run and name that
run and its commit. The separate candidate audit and its retained CI evidence do not constitute those reports.
`collect` and `promote` require a completed run for the current commit and reject tracked working-tree changes.
These checks do not lock
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
- **Targeted promotion.** `bin/release-evidence promote` verifies all four CI platforms by default. Pass
  `--platforms lin-x64` (or an explicit list) only when the release advertises that narrower target set. A
  declaration is promoted only if it is available on at least one selected target and its cited tests pass on
  every selected target where it is available. Other platform evidence remains in the collected reports but
  does not silently become a support claim; `check_coverage.py release` must still pass for every advertised
  target.
- **Shared checker tests.** The synthetic coverage-checker mutation suites run once in the Linux unit job rather
  than on each OS. They are not binding or sample evidence, and a regression test forbids citing them as such.
  Collection reports a release-bar problem, and promotion refuses to run, if that shared job is missing or fails.
  All actual record and sample evidence remains specific to its target platform.

`verified` needs a reviewer (RP-42 step 6: a human or an explicitly authorized agent workflow). Until the owner
chooses one, no record is verified and the release bar stays closed on every platform. That is the expected
state, not a failure to hide.

## Open gates

Reported, not waived:

- No real GPU: CI uses software OpenGL for applicable tests. In the candidate install checks of
  [run 42263701004](https://github.com/feoh/RackPy/actions/runs/42263701004), the macOS and Windows runners
  could not create a Rack OpenGL window; their windowed candidate checks remain open.
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
