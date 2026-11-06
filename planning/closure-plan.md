# Closure plan: the remaining work, restructured

Written 2026-11-06 at the owner's request. It restructures the work that
remained after the Linux 1.0.0 candidate so that each claim has its own task
and gate, and the independent review stops being invalidated by every fix. It
changes no acceptance criterion, coverage denominator or safety gate. Dates
are a ballpark the owner asked for, not commitments; the assumptions they rest
on are listed at the end.

## Why the previous shape took days

The RP-42 comment trail from 2026-11-02 to 2026-11-06 shows the loop. Every
batch of fixes produced a new candidate SHA. Under the exact-SHA rule that
discarded every review verdict, so all 3,107 records were re-reviewed and a
full CI run (about an hour after sharding, about ninety minutes before) was
started again. Reviewer slices often came back structurally incomplete and
needed retries. At least fifteen such cycles ran, each finding fewer defects
than the last, and none promoted a record. Provider limits and credits paused
the work twice. Four unrelated claims were also gated by one task: the coverage
proof, cross-platform sample parity, the platform releases and the performance
certification.

Three owner decisions on 2026-11-06 already address the first two causes: the
fourteen listed known release gaps, the Linux-only audio-guard proof for the
four guard-instrumented samples, and incremental review with carry-forward of
verdicts for byte-identical records. This plan turns those decisions into
tasks and rules.

## The streams

Each stream is one or more tasks in [TASKS.md](TASKS.md) and live Witan
([slugs](witan.json)). The streams are independent of each other except where
a dependency is listed.

| Stream | Tasks | Gate | Bound by |
| --- | --- | --- | --- |
| Publish the authorized 1.0.0 Linux release | RP-45 | Tag on the verified source commit, public bytes verified | Half a day of agent time |
| Full-coverage proof | RP-46 (tooling and protocol), RP-47 (Windows evidence), then RP-42 | `check_coverage.py release` passes per advertised target with carried and fresh verdicts | Two to three working days, credits permitting |
| Performance certification | RP-48 | Controlled measurements against ADR 004 budgets, reproduced once | One owner hardware session plus one rerun |
| macOS release | RP-49 | Hardware verify from the published bytes on both architectures | Owner access to a Mac per architecture |
| Windows release | RP-50 | Hardware verify from the published bytes | Owner access to a Windows PC with a GPU |
| Optional follow-ons | RP-51 (guard port), RP-52 (permissive examples), RP-53 (PyPI and VCV Library) | Each its own | Do not block the epic |

The epic is blocked by RP-42, RP-45, RP-48, RP-49 and RP-50 and by nothing
else. RP-42 is blocked by RP-46 and RP-47 in addition to its original
prerequisites.

## Review protocol (RP-46 implements and documents it)

1. **Freeze first.** A review round runs against one candidate SHA. No fixes
   land on that branch while the round is open; findings are collected.
2. **Carry forward.** A verdict carries from the previous reviewed commit when
   the record's manifest entry, its cited test files and the sources named by
   its mapping are byte-identical. Everything else is the redo set. The
   prototype carried 1,901 of 3,107 records from 2014699 to 4fba8b9.
3. **Small, validated chunks.** Redo records go out in chunks of at most 25
   with the exact record list. Each reviewer output is validated structurally
   against that list; only failed chunks are retried; placeholders are never
   accepted.
4. **Batch the fixes.** After a round, adjudicate every finding, fix the
   verified ones in one batch, and re-review only the records the batch
   changed.
5. **Converge, then promote.** A round with zero verified findings promotes its
   verdicts. Only the promoting run needs a green exact-SHA CI; review rounds
   themselves do not wait on CI.
6. **Nothing weakens.** The denominator, the `verified` meaning, the exact-SHA
   guard for changed records and the platform-specific test evidence stay as
   they are.

## Ballpark dates

Assuming the work starts on 2026-11-09 with credits available and the owner
can give one hardware day each for performance, macOS and Windows:

| Milestone | Target |
| --- | --- |
| 1.0.0 Linux published (RP-45) | 2026-11-09 |
| Full coverage proven on Linux and the other advertised targets (RP-46, RP-47, RP-42) | 2026-11-14 |
| Performance certification (RP-48) | 2026-11-18 |
| macOS release (RP-49) | 2026-11-21 |
| Windows release and epic closure (RP-50) | 2026-11-27 |

Pessimistic case, with an ADR 004 budget failure or a serious platform defect
found on real hardware: mid-December 2026. The performance session and the
platform validations need only the owner's hardware time, so they can overlap
the coverage work rather than follow it.

## Assumptions behind the dates

- The observed costs hold: a sharded exact-SHA CI run of about an hour
  (run 42284808506 took 54 minutes), review rounds of one to two hours for
  the full record set when reviewers return complete output, and a redo set
  of roughly 1,200 records for the first carried round.
- Findings keep declining as they did from round to round (545, 168, 7, 22,
  18, 35, then single digits per pass), so two or three rounds converge.
- Subagent credits are available for the review rounds; the previous loop
  stopped twice on limits.
- Real-hardware runs find a few defects each, as the first Windows run in
  RP-41 did; each is fixed and the final bytes re-verified.
- ADR 004 budgets pass or are reported as explicit failures. An investigation
  of a failed budget is time-boxed and does not stretch the date silently.

## What this plan does not do

It does not publish anything, merge the release branch to main, change any
CI gate, close RP-42 or the epic, or mark any record verified. Those remain
the tasks above, each with its own evidence.
