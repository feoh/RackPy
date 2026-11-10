# RP-57 validation: the macOS release check when Rack dies in its MIDI start-up

Task: `tk-rackpy-57-macos-release-candidate-check-rack-abo-cdeb18`, 2026-11-10.

## The failure

Rack Free 2.6.6 on GitHub's `macos-15-intel` runner died at start-up in two of the 32 mac-x64 release-candidate
jobs that ran between 2026-11-04 and 2026-11-10: run 42068116544 (case `quarantined-package`) and run
42834516722 (case `RackPyTemplate-2`). Rack's own output, both times:

```text
MidiInCore::initialize: error creating OS-X MIDI client object (-50).

libc++abi: terminating due to unexpected exception of type RtMidiError: MidiInCore::initialize: error creating OS-X MIDI client object (-50).
```

Rack starts its MIDI driver before it loads plugins: a Rack log has `Initializing MIDI` and then
`Initializing plugins`, and RackPy is first mentioned after the second. So no RackPy code had run. Why
CoreMIDI refused Rack a client on those two launches is not known. The check starts Rack 39 times in a job
with ten example plugins, so this is two of about 1,250 launches.

Not seen elsewhere: none of the 32 mac-arm64 candidate jobs of the same days shows it, and no log of the 47
failed macOS jobs (suites and candidates) has a Rack log that stops at `Initializing MIDI` or a Rack exit
status of -6. The pytest launcher (`run_rack` in `tests/support.py`) and `bin/rackpy-release introspect` start
the same Rack the same way, so they could meet it; both discard Rack's standard error, so it would show as
exit status -6 with a log ending at `Initializing MIDI`. One difference, which may or may not matter: the
release check runs Rack with an empty environment and an empty home folder, and those two do not. Neither was
changed.

## The decision

Owner, 2026-11-10: running Rack once more is acceptable. The project's rule against retries stands
(AGENTS.md); this is the one exception, and it is of Rack's start-up, not of a test.

## What changed

`packaging/macos/verify.sh` runs Rack through `relaunching`, for its three kinds of run (headless, the
OpenGL probe, and screenshot mode). A run is repeated once when all of these hold:

- Rack ended by a signal (exit status above 128);
- its output has `MidiInCore::initialize: error creating OS-X MIDI client object`;
- its log has `Initializing MIDI` and does not have `Initializing plugins`.

The first run's files are kept as `<case>.midi-abort-N.*` in the evidence. The summary gets a line
`AGAIN <case>: Rack died in its own MIDI start-up (exit status 134) before loading any plugin; run once
more, …`, a count before the result line, and on GitHub Actions a warning on the run's page. If the second
run dies the same way the summary says `FAIL  <case>: Rack died in its own MIDI start-up twice, …` and the
job fails; before, the cases whose exit status is not checked (`extract`, `before-upgrade`) would not have
named it. Nothing else is repeated.

## What was checked

The script's own functions against a stand-in for Rack (`tests/unit/test_macos_verify_relaunch.py`, 8 tests,
7 s), on Linux with bash 5.3.20, and the same cases by hand in the `bash:3.2` container image (bash 3.2.57,
the version of macOS's `/bin/bash`), with the same results:

| Stand-in Rack | Runs | Result |
| --- | --- | --- |
| dies in MIDI start-up once, then works (each of the three kinds of run) | 2 | passes; `AGAIN` line; first run kept |
| dies in MIDI start-up every time | 2 | `FAIL`, named as Rack's; not run a third time |
| killed by a signal with another message | 1 | not repeated |
| the MIDI message, after plugins began to load | 1 | not repeated |
| the MIDI message, exit status 1 | 1 | not repeated |
| works | 1 | not repeated |

`bash -n` under both versions, `shellcheck -S warning` clean, hooks and the unit tests pass.

## What was not checked

- The death itself. It cannot be provoked; the first one on CI will show as an `AGAIN` line and a warning.
- The changed script on a Mac before landing. The first run of the macOS release-candidate jobs after this
  commit is that check, and its result is recorded on the Witan task.
- Rack in screenshot mode on a Mac after the change: GitHub's runners give Rack no OpenGL window, so that
  path runs only on a Mac with a display.
