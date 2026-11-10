# RP-56 validation: CI wall-clock and failures

Measured 2026-11-10 with `gh` against `feoh/RackPy`. Times are from a run's creation to the end of its last
job, so they include runner queueing. Each row is what one set of runs did, not a promise.

## Before

The 40 CI runs from 2026-11-04 22:32 UTC to 2026-11-10 14:29 UTC: 15 succeeded, 23 failed, 2 were cancelled.
Job data was read for 39 (the API returned 502 for run 42249036424, a failure).

### Where the time went

31 of them ran every job (pushes to `main` and ordinary dispatches) and were not cancelled.

| Measure | Median | Shortest | Longest |
| --- | --- | --- | --- |
| Whole run | 59.4 min | 44.0 min | 94.5 min |
| Until the last test job ended (everything except the distributable build, the release candidates and the per-platform SDK inventory) | 31.5 min | 17.9 min | 90.0 min |

In [run 42834516722](https://github.com/feoh/RackPy/actions/runs/42834516722) (50.7 minutes, warm compiler
caches) the last test job ended at 21.4 minutes. What ran after that:

| Job | Started | Ended | Why it was late |
| --- | --- | --- | --- |
| Distributable build | 0.1 min | 23.3 min | `pytest -m dist`, two tests, 22.6 minutes |
| Linux release candidate | 23.4 min | 50.7 min | waited for the distributable build, only to share its toolchain cache |
| Windows x64 release candidate | 0.1 min | 40.0 min | 33.5 minutes of it compiling, with no compiler cache by design |
| macOS mac-arm64 release candidate | 11.3 min | 41.9 min | queued behind the macOS test jobs: GitHub runs five macOS jobs at a time and a run has fourteen |
| macOS mac-x64 release candidate | 0.1 min | 20.6 min | held a macOS runner the test jobs were waiting for |

The compiler caches: 36 caches held 10.4 GB against GitHub's 10 GB limit. Each commit saved a new set of five
(about 2.5 GB) and nothing removed the old ones, so GitHub evicted by last use. On 2026-11-10 `main` had no
compiler cache for lin-x64, win-x64 or mac-arm64. A branch can read only its own caches and `main`'s, so a new
branch's first run compiled from nothing: test jobs of 42 to 67 minutes instead of 3 to 12.

### What failed

The 22 failed runs whose logs were read, sorted by `bin/ci-failures`' rule:

| Kind | Runs | Detail |
| --- | --- | --- |
| Hooks only | 7 | `trim trailing whitespace` on `release-evidence/verify-public-e329b10/summary.txt`, pushed to `main` in `23decf7` on 2026-11-07 and inherited by every branch: every run from then until 2026-11-10 03:05. The hook failed within 30 seconds; the runs took 8 to 74 minutes |
| A test failed on Linux or on two or more platforms | 9 | all on 2026-11-05 and 2026-11-06, mostly `rp42-release-audit`; three of them also failed the hooks |
| Tests failed on one of macOS or Windows only | 4 | 42061405852, 42068116544, 42101882241, 42118378141 |
| Outside the tests | 2 | external link check rate limited (41937530408); mac-x64 release candidate, `Abort trap: 6` (42834516722) |

## After

Not yet measured: this file is updated with the first tests run and the first candidates run of the changed
workflow.
