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

### The intermittent ones, by cause

Read from the failed jobs' logs, the artifacts of four of the runs and the source at `1fc30a9`. "Verified"
means the log or artifact shows it; "inferred" means the source allows it and nothing contradicts it.

| Failure | Seen | Cause | Task |
| --- | --- | --- | --- |
| mac-x64 release candidate: `Abort trap: 6`, then exit status -13 | 2 of 32 mac-x64 candidate jobs (42068116544, 42834516722) | Verified. Rack aborts in its own MIDI start-up (`MidiInCore::initialize: error creating OS-X MIDI client object (-50)`, an uncaught `RtMidiError`) before it loads a plugin. `verify.sh` then wrote the quit line to the fifo of the dead Rack and died of SIGPIPE, so the case was not named and the later cases did not run | The SIGPIPE is fixed here; the abort is RP-57 |
| `test_python_reads_snapshots_while_racks_engine_runs`: `snapshot()` raised | 3 jobs (mac-arm64 in 42101882241 and 42105233104, Windows in 42105233104) | The read gave up after 1000 spins with no pause: verified. That the engine thread was descheduled in mid-write: inferred. `db07fee` put a retry in the test, and the exception is not in the documentation | RP-58 |
| `test_reset_rate_bypass_and_failure_are_native`: no `kernel_failed` diagnostic | 1 job (mac-arm64, 42068116544) | Inferred. `KernelSlot::fail` publishes the flag before the diagnostic record. Not an `until(...)` budget. `cd94971` made the test poll for both | RP-59 |
| `test_shutdown_runs_in_the_documented_order`: a marker missing | 1 job (mac-x64, 42061405852) | Verified. The marker is logged by an unjoined daemon thread that had about 5 ms before Python was finalised. `8596f98` made the marker optional | RP-60 |
| CTest `test_runtime.cpp:141: CHECK failed: waited >= 40` | 3 jobs (42105233104, 42118378141) | A lock test that assumed a 40 ms wait. Fixed at the source by `06122b8`; not seen since | none needed |
| A download or a request to someone else's server failed | `upload-artifact` could not resolve a host (42118378141); external link check rate limited (41937530408); HTTP 500 for the managed CPython archive (42849726853, below) | Every Linux job downloads CPython from a GitHub release (`cache-python: false`); the link check runs on every tests dispatch | RP-61 |

No test's own time budget ran out in any of these. What the four test failures share is an assumption that
another thread finishes a step inside a short real-time window: about a microsecond of spinning, the gap
between two stores, 5 ms, 40 ms.

The fix to `verify.sh` was checked by running the script's own `headless` function against a stub that prints
Rack's message and aborts: at `70d3e65` the script dies with status -13, as in CI; with the change it writes
134 to the case's `.rc` file, copies the log and goes on. With a stub that waits for the quit line, both
versions record 0. Run on Linux with bash 5; macOS's bash 3.2 was not tried.

## After

### The first push to `main`: every job, cold compiler caches

[Run 42851605565](https://github.com/feoh/RackPy/actions/runs/42851605565), the push of `5ad90f3`: 41 jobs
succeeded, none failed, and only the external link check was skipped, as on every push before. It took 95.0
minutes; the last test job ended at 76.6 minutes. That is not the workflow's ordinary time, for two reasons
that were known before it started: `main` had no compiler cache for lin-x64, win-x64 or mac-arm64 (above),
and for its first 14 minutes a tests dispatch on the branch (42849726853, cancelled at 16:22 UTC) was using
macOS runners too, four of the five when it was cancelled.

| Job | Started | Ended | Note |
| --- | --- | --- | --- |
| Official Rack toolchain | 0.1 min | 0.2 min | cache found; nothing downloaded or built |
| Distributable build | 7.5 min | 32.2 min | queued for 7 minutes |
| Linux release candidate | 6.2 min | 44.0 min | queued for 6 minutes; ran beside the distributable build, not after it |
| Windows x64 release candidate | 2.7 min | 41.0 min | |
| macOS mac-arm64 release candidate | 53.5 min | 81.0 min | queued behind the macOS test jobs |
| macOS mac-x64 release candidate | 62.5 min | 95.0 min | queued behind the macOS test jobs |
| Keep one compiler cache per platform | 76.7 min | 76.8 min | after the last test job |

With cold caches the test jobs took 39 to 48 minutes each on mac-arm64, 50 to 71 on Windows and 51 for the
Linux host suite (3 to 10 on mac-x64, which had a cache). Each mac-arm64 job held one of the five macOS
runners that long, so the two macOS candidates started 53 and 62 minutes in.

After the run `main` held exactly one compiler cache per platform, all saved by this run (lin-x64 690 MB,
win-x64 584 MB, mac-arm64 471 MB, mac-x64 467 MB, native lin-x64 342 MB); the two it had from `1fc30a9` were
gone. The repository held 10.06 GB in 37 caches, 4.66 GB of it on another branch (mostly two older sets of
compiler caches). The cache job leaves those alone: it removes only caches of the branch it runs on.

The tests dispatch on the branch was cancelled before it finished, so it measures nothing. It did show that
the six candidates jobs are skipped on a plain dispatch, that the cache job runs after a cancellation, and
RP-61's failure (two jobs lost to one HTTP 500).

### The second push to `main`: every job, warm compiler caches

[Run 42868485668](https://github.com/feoh/RackPy/actions/runs/42868485668), the push of `251054a`, with no
other run in progress: 41 jobs succeeded, none failed, the external link check skipped. It took 41.0 minutes,
and the last test job ended at 15.2 minutes.

| Measure | This run | Before: warm run 42834516722 | Before: 31 full runs, median (shortest) |
| --- | --- | --- | --- |
| Whole run | 41.0 min | 50.7 min | 59.4 min (44.0) |
| Until the last test job ended | 15.2 min | 21.4 min | 31.5 min (17.9) |

| Job | Started | Ended | Ran |
| --- | --- | --- | --- |
| Official Rack toolchain | 0.1 min | 0.3 min | 0.2 min |
| Distributable build | 2.2 min | 26.5 min | 24.3 min |
| Linux release candidate | 2.2 min | 32.1 min | 29.9 min |
| macOS mac-x64 release candidate | 6.0 min | 32.9 min | 26.9 min |
| Windows x64 release candidate | 0.4 min | 38.9 min | 38.5 min |
| macOS mac-arm64 release candidate | 11.3 min | 41.0 min | 29.7 min |

The platform test suites took 2 to 10 minutes each. What sets the length of a full run now is the
candidates themselves: the Windows one runs for 38 minutes, most of it compiling with no compiler cache by
design, and the mac-arm64 one waits 11 minutes for one of the five macOS runners and then runs for 30.

One run is one run: the 9.7 minutes against the earlier warm run is a single comparison, not a distribution.

### A tests dispatch on `main`, warm compiler caches

[Run 42875698756](https://github.com/feoh/RackPy/actions/runs/42875698756), `gh workflow run ci.yml --ref main`
at `251054a`, with no other run in progress: 33 jobs succeeded, none failed, and the six candidates jobs
(toolchain, distributable build, the release candidates, per-platform SDK inventory) were skipped. It took
20.0 minutes. The last job to end was the mac-x64 services suite, which started at 6.6 minutes and ran for
13.2; the same suite ran for 9.9 minutes in the push above, where the last test job ended at 15.2 minutes.

So the question "do the tests pass on four platforms" was answered in 20 minutes here, against a median of
59.4 for the run that used to answer it. That is one run.

`bin/release-evidence collect 42875698756`, written to a scratch directory, judged the tests, records and
samples of all four platforms and reported the release bar as "not judged" for each, with the skipped
candidates jobs listed as not successful. A tests run cannot be mistaken for release evidence.

### A candidates dispatch on `main`, warm compiler caches

[Run 42879734745](https://github.com/feoh/RackPy/actions/runs/42879734745), `gh workflow run ci.yml --ref main
-f candidates=true` at `251054a`, with no other run in progress: all 42 jobs succeeded (the 41 of a push, and
the external link check, which a dispatch runs and a push does not). It took 43.4 minutes; the last test job
ended at 24.5 minutes. The last job was the mac-x64 release candidate, 5.3 to 43.4 minutes; the Windows one
ran from 0.1 to 40.6. The distributable build (2.0 to 22.4) and the Linux release candidate (2.0 to 31.6)
started together.

### Summary

| Run, warm compiler caches, nothing else running | Jobs | Time | Before |
| --- | --- | --- | --- |
| Tests dispatch (42875698756) | 33 | 20.0 min | no such run; the full one, median 59.4 min |
| Push to `main` (42868485668) | 41 | 41.0 min | median 59.4 min, shortest 44.0, of 31 full runs |
| Candidates dispatch (42879734745) | 42 | 43.4 min | the same |

Three runs, one of each kind. A run with cold compiler caches took 95.0 minutes (42851605565).

### CPython on the Linux jobs (RP-61)

In run 42851605565, 18 of the 19 Linux jobs that set up uv downloaded CPython 3.14.2 (33.7 MiB) from a GitHub
release; the other, the toolchain lookup, never ran uv. With `cache-python: true` on those jobs (`f652632`), two fast dispatches on
the branch:

| Run | Python cache | Linux jobs | Downloads of CPython |
| --- | --- | --- | --- |
| [42860073165](https://github.com/feoh/RackPy/actions/runs/42860073165) | none yet | 3 finished before the next dispatch cancelled it | 3; one job saved the cache (36 MB) |
| [42860483645](https://github.com/feoh/RackPy/actions/runs/42860483645) | saved by the run above | 16 started (12 succeeded, 4 cancelled by hand after their uv steps) | 0; all 16 restored it, and Python is still 3.14.2 |

On `main`: in the push of `251054a` (42868485668) 14 Linux jobs missed, downloaded and saved, and the 4 that
started after the save restored it. In the tests dispatch after it (42875698756) all 17 Linux jobs, the
external link check among them, restored it, and no log has a CPython download.

In the candidates dispatch (42879734745) all 20 Linux jobs restored it, the toolchain lookup, the
distributable build and the Linux release candidate among them; again no log has a CPython download.

The key includes `pyproject.toml` and `uv.lock`, so the first run after either changes downloads again.
