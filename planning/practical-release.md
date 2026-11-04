# Practical path to a first public release

This is the working delivery plan for RP-44, not a claim that a release is
complete or permission to publish. It applies the owner's practical-delivery
amendment in [the execution plan](README.md#owner-amendments). Full coverage
of the supported public Rack API remains a long-term goal; this plan does not
change the coverage denominator or call incomplete mappings complete.

## Remaining work, in order

1. **Choose a small, honest release scope.** Recommend an early, partial Linux
   x86_64 release for Rack 2.6.6, with the actual artifacts and version selected
   at approval time. The local `0.1.0.dev1` preview is not itself approved for
   publication. The owner heard its oscillator through headphones in Rack Pro
   2.6.6; that one smoke complements the Linux Rack Free container checks, but
   is not a broad Pro compatibility or performance certification. Windows and
   macOS can follow independently after their own candidate checks; no need to
   delay Linux for parity.
2. **Make the first-user journey work.** From a clean checkout/environment,
   follow the documented path to build or use the candidate, install one
   example in real Rack, hear or observe its output, save and reopen a patch,
   and remove it. Check a developer can start from the template and find how
   to package a plugin. Fix confusing instructions and material failures found
   in that journey rather than auditing every declaration or porting every
   official sample before an early release. Document limitations prominently,
   including the one-RackPy-plugin-at-a-time constraint.
3. **Validate the candidate once, proportionately.** Build from an identified
   clean commit; verify checksums, pinned inputs and license notices, and run
   the relevant automated unit/native/audio-path and real-host checks. Exercise
   install, upgrade and uninstall on the advertised platform using the actual
   candidate bytes. Reuse recorded results for unchanged bytes instead of
   rebuilding or repeating the same suite by habit. If code or artifacts change,
   rerun the checks affected by that change and verify the final bytes; do not
   imply that tests on an earlier build validate new binaries. Keep crash,
   lifetime and audio-path regression checks; never trade away the production
   audio-path constraints for speed.
4. **Prepare a short release handoff.** Audit the README, version, release notes,
   installation guide, known limitations, artifact list, license/source
   availability, checksums and evidence against what is actually shipped. Say
   what was not tested; do not claim complete coverage, multi-platform parity,
   real-time guarantees or benchmark results without evidence. Identify any
   release-blocking defect and defer lower-impact improvements explicitly.
5. **Ask for publication approval.** Present the exact version, commit,
   artifacts, destinations and release notes, and any signing/VCV submission
   decisions. Only after separate owner approval, publish the approved items;
   check the downloaded public bytes and a representative installation. Local
   preview approval does not authorize a tag, upload, redistribution or VCV
   contact. Complete RP-44 only when its actual approved delivery is verified.

## Official samples as a gap-finding baseline

A fresh inventory check is a better starting point than creating duplicate
ports; see the detailed [sample API audit](sample-api-audit.md):

- Live Witan shows RP-33 through RP-36 closed. `upstream/examples.json` has 77
  entries; all tutorial, API-guide and helper/template samples have tested
  dispositions. `bin/manual-sources check` on 2026-11-04 confirmed all 69
  captured code blocks still match the live manual. Four Rack-building samples
  (`building-06`–`building-09`) are explicitly out of scope because they build
  Rack itself, not a plugin.
- In CI run
  [41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030), the
  sample evidence from commit `16ecf405932b857197406673bec1d16ff09115c5`
  reports 73 disposition tests passed on Linux. macOS arm64, macOS x64 and
  Windows each report 69 passed and four skipped: the same Linux-specific
  audio-path guard tests (`guide-07`, `guide-13`, `guide-25`, `guide-30`).
  These are platform-evidence gaps for that guard, not unported sample features;
  the functional sample tests passed. This older run is not evidence for later
  candidate bytes.
- The newer local preview's ten plugins also passed its Ubuntu 20.04 clean
  install/run checks, and the owner manually heard the oscillator in Rack Pro.
  Neither establishes full API coverage.

No official plugin-facing sample remains to port from this captured manual
revision. For sample-led API work, first map a concrete user workflow in an
example to its binding-manifest records and passing tests; prioritize only a
real missing or unusable operation. Do not reopen RP-33–RP-36 to repeat their
completed deliverables. The cross-platform audio-guard skips may be addressed
separately if that platform claim becomes useful, but they are not the first
API feature to implement.

## Proposed lean path to cross-platform support and eventual parity

These are **planning recommendations**, not a replacement for the existing
[full-surface release bar](../docs/coverage.md#release-bar), an assertion that
any platform has passed it, or authorization to publish. Apply the same
principle as the first-release plan: pay for evidence of a particular claim,
not repeated work that provides no new information. Keep these claims separate:

| Claim | Evidence needed | What it does not establish |
| --- | --- | --- |
| A platform is usable for an early release | On that native OS and architecture, verify the identified final candidate bytes: integrity and dependencies, real Rack load, a representative create/package/install/use/save/reopen/uninstall journey, relevant crash/lifetime/audio-path checks and actionable failure handling. Document environment, skips, limitations and security/signing behavior. | Full SDK coverage, equal platform features, all OS versions, or real-time performance. |
| Cross-platform release | Meet that platform bar independently for every advertised OS/architecture and say exactly which versions and features were exercised. Ship only the candidates that meet it; do not infer a platform pass from cross-compilation, another platform, or earlier bytes. | Feature parity or complete coverage. One platform's delay need not hold back another partial release. |
| Parity for a named feature set | Define the feature set and version, then compare its meaningful behavior, error handling and documented differences on each claimed target using native host tests where necessary. Record intentional platform differences and unresolved skips; do not equate matching package names with matching behavior. | Parity outside that feature set or full Rack SDK coverage. |
| Full supported public API coverage | Meet RP-42 and the existing coverage contract for **every** advertised platform, including the full inventory, semantic Python mappings, evidence for platform availability, callbacks/macros/templates and exclusions, tests/docs, and independent review. Native-only or inaccessible mappings, unknowns and skipped required evidence are not passes. | Universal platform support, physical-device deadlines or benchmark claims. |

### Reduce work without reducing evidence

1. **One platform matrix, not three unrelated checklists.** Identify Linux
   x86_64, macOS x86_64/arm64 and Windows x86_64 as candidate targets, not
   blanket support claims. For each release, record source commit, artifact
   hashes, native OS version, host version, checks and explicit pass/fail/skip.
   A single candidate-verification run can satisfy multiple gates when its
   evidence actually covers each one. Reuse preserved evidence for identical
   bytes and conditions; rerun affected checks for changes and always verify
   the final bytes. No cross-platform substitution for platform-specific tests.
2. **Test the real user journey once per candidate and target.** Combine
   template-to-package, actual Rack installation, representative UI and native
   audio operation, patch round-trip, upgrade and uninstall with the existing
   candidate suite where practical. Retain focused platform-specific tests for
   loader isolation, runtime collisions, paths and distribution/security behavior.
   Host smoke is not a substitute for an audio-device deadline measurement.
3. **Treat missing evidence as a scoped gap, not an automatic project-wide
   veto.** Prior Mac validation records windowed-render skips on CI and SIP-on
   behavior not established; Windows records Linux-only dynamic audio guards
   and sanitizer gaps. For a platform claim, investigate whether the real user
   journey and a meaningful safety check can be run on a suitable native host,
   or whether a different check actually tests the same risk. Record its scope
   and residual risk; if it does not, keep the gap open and narrow the claim.
   Do not mark a skip as a pass or weaken a required full-coverage gate.
4. **Use sample parity as a feature map, not duplicate implementation work.**
   Reuse the 77-entry inventory and existing feature dispositions. For a named
   parity claim, run the relevant actual-candidate behavior on each target;
   identify out-of-scope source blocks and skipped tests. The prior Linux-only
   audio-guard skips remain gaps in guard evidence, not proof that a sample's
   feature is absent. Full sample parity remains subject to RP-42's existing
   stricter evidence requirements.
5. **Make full coverage incremental.** Preserve all counted public entries and
   their semantic requirements. Prioritize gaps that block real plugin workflows;
   group independent review and tests by family and reuse reviewed results only
   where the reviewed source, mapping, tests and platform evidence still apply.
   Automate change-impact and evidence collection where reliable, but retain the
   owner-approved independent review and spot checks. Re-review changed or
   invalidated evidence; do not replace review with introspection alone. Until
   RP-42 passes, label coverage partial regardless of progress percentages.
6. **Run expensive measurement for the claim it supports.** The controlled A/B
   and physical-device deadline work supports performance claims, not a general
   cross-platform availability claim. Preserve meaningful audio-path and
   lifecycle safety checks on every advertised target; document where an
   equivalent platform mechanism has not been demonstrated. Never claim audio
   deadlines, lock-free host behavior or a passing benchmark without evidence.

The earlier multi-platform CI candidate used source commit
`16ecf405932b857197406673bec1d16ff09115c5` ([run 41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030));
its platform jobs and known skips are useful starting evidence, **not** a
pass for a newer candidate. The current local preview audit covers only its
identified Linux bytes. No CI gates, coverage denominator, task acceptance
criteria or owner approvals are changed by these recommendations.

## Test effort: pay for information, not repetitions

- On ordinary changes, use hooks and focused tests for the changed area. Run a
  real Rack host test when host behavior is affected, and relevant native and
  audio-path checks when native or audio code changes. Documentation-only
  changes need the documentation checks, not a fresh platform release matrix.
- At a release candidate, run the appropriate final checks once for the
  advertised platform and exact candidate. Additional repetitions need a
  concrete reason (for example, diagnosing a race), not an arbitrary count.
  Save reports and elapsed times so later decisions use evidence.
- Expensive sanitizer, stress, cross-platform, exhaustive inventory/review,
  and controlled benchmark runs are targeted to relevant changes, scheduled,
  or performed when expanding a support/coverage/performance claim. They are
  not automatically prerequisites for a partial Linux release. This is a
  proposed cadence, not a claim that existing CI has been reconfigured.
- Investigate flaky failures: preserve the first failure, determine whether it
  indicates a product defect or test/environment problem, and fix or explicitly
  quarantine a demonstrably non-critical unreliable check with a tracked reason.
  Do not silently rerun until green or quarantine meaningful crash, lifetime,
  security or audio-path failures just to ship.
- Do not change the coverage inventory or mark records verified simply to
  produce a green release check. Keep the stronger evidence requirements for
  any future complete-coverage claim.

## Measured costs and existing evidence

In [CI run 41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030)
for earlier commit `16ecf405932b857197406673bec1d16ff09115c5`, the longest job
(macOS x64 native/host) took approximately 81 minutes; Linux host took 53
minutes, docs 14 minutes, and Linux candidate verification 34 minutes after the
build dependency. Jobs ran in parallel, so their durations are not additive
wall-clock time. The optional external link check failed; the other listed jobs
passed. This is one observed run, not a typical-time estimate or evidence for
the current candidate. External links are already on-demand/weekly, not a
pull-request gate. Measure repeated timings and failures before optimizing
particular jobs; a hypothetical 15-hour suite or flaky test is not an observed
fact here.

The exact local preview's checksums, container checks and caveats are in the
[candidate audit](../release-evidence/candidate-audit.md). The owner-reported
headphone smoke is recorded on RP-44 in live Witan. Neither replaces testing the
final public candidate if its bytes change.

## Deferred, not erased

Full SDK surface and sample parity, complete per-record independent review,
all-platform parity and a controlled performance certification remain useful
future work, but are not blockers for an honestly scoped first release. The
long-term full-surface epic remains open until its own goal is met or the owner
explicitly revises it. Do not close it merely because a partial release ships.
