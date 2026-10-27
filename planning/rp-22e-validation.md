# RP-22E validation (Linux x86_64)

Date 2026-10-26, branch `rp22-laboratory-2`, based on `origin/main`
`9baa5b05105fe8634fb4b6d27c083b7d645941de`, GCC 16.2.1, bundled CPython
3.14 and pinned Rack Free/SDK 2.6.6. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). All builds use this worktree's
own `build/`; host runs are serialized with
`flock /tmp/rackpy-host-tests.lock`. No audio or MIDI device is opened.

RP-22E (`tk-rackpy-22e-laboratory-python-override-of-dsp-mid-5172f2`) adds the
laboratory Python path for
`method:rack::dsp::MidiGenerator::onMessage(const rack::midi::Message &)`.
The manifest record moves from `planned` to `implemented`, **not
`verified`**; the coverage denominator is unchanged. Linux totals remain
13 unreviewed, 2 absent and 0 verified, so this is not a full-coverage or
release claim. Decisions: [rp-22-slices.md](rp-22-slices.md); explanation:
[laboratory MIDI generator](../docs/explanation/callbacks.md#a-laboratory-midi-generator).

## Recovery and implementation review

- Reviewed the recovered, uncommitted implementation and its untracked
  in-Rack test; saved it as `WIP RP-22E (recovered after restart)`, then ran
  `git fetch origin && git rebase origin/main`. The generated-file
  conflicts were resolved with `uv run bin/generate_bindings.py generate`,
  not hand-merging. The recovered checkpoint after rebase was `317f9fd`.
- The trampoline's base is RackPy's existing `RecordingGenerator`, not the
  empty Rack base `onMessage`: `super().on_message(message)` records for
  `take_messages()`. Ordinary raw generators still have no public
  `on_message` override point. Python receives an owned `Message` copy,
  safe to retain after Rack's stack-local message and generator are gone.
- **Opt-in bypass found and fixed:** the recovered `_refuse_on_message`
  inspected only `vars(cls)`, so an inherited mixin callback could reach
  the new trampoline without laboratory enablement. Class/instance
  monkeypatching and deliberately bypassing the laboratory constructor
  could bypass a Python-only guard too. With supervisor approval, the raw
  guard now checks the MRO, and native dispatch also requires a
  per-instance laboratory mark. The private marking path checks native
  laboratory enablement; unmarked generators always use the recorder.
  Tests cover mixins, late class/instance monkeypatches, a spoofed Python
  marker and a laboratory subclass using the raw base constructor.
- **Supervisor-approved test hook:**
  `_rackpy_host._testing.midi_generator_16_on_rack_thread` resolves the
  bound generator under the GIL, releases it, binds a Rack context on a
  worker and invokes the real native virtual. The hook and its resolver
  are compiled only with `RACKPY_TEST_HOOKS`; the typing declaration is
  updated too. Its test requires no Python callback, exactly one
  `refused_rack_thread` increment and the native recorder's bytes/frame.
- Laboratory creation requires `rackpy.laboratory.enable()`. These
  Python-owned generators are not registered with Rack's engine or MIDI
  drivers. Like laboratory quantities, disabling the laboratory prevents
  new laboratory construction; it does not revoke existing generators.

## Dispatch limitation found in RP-22E; subsequently corrected

The original RP-22E tests pinned a divergence: pybind11's `get_override`
Python-frame check suppressed same-object/same-name native reentry and sent
the nested generator message to the recorder instead of the Python override.
That was an explicitly documented limitation of this historical slice, not
C++-equivalent behavior.

The cross-family follow-up
`tk-rackpy-trampolines-suppress-same-object-re-entra-0abce9` replaces that
resolver and audits every super path. The pinned test is now
`test_reentrant_same_generator_dispatches_as_cpp`: Python receives both the
note-on and the nested controller message; explicit `super()` records each
once, in callback completion order. Unbounded native reentry raises
`RecursionError` at 64 live override calls. See
[the follow-up audit and validation](trampoline-reentry-validation.md) for
fresh evidence; the original counts below are historical, not rerun counts.

## Commands and outcomes

| Command | Result |
| --- | --- |
| `CMAKE_BUILD_PARALLEL_LEVEL=2 bin/rackpy-dev build --werror --test-hooks` | Pass: native build, CTest 6/6; repeated after native enablement hardening. |
| `CMAKE_BUILD_PARALLEL_LEVEL=3 flock /tmp/rackpy-host-tests.lock uv run pytest -m "host and not dist" tests/laboratory/test_laboratory.py tests/dsp/test_basic.py --basetemp=build/validation/pytest` | Initial run: 3 passed in 983.08s. The production laboratory suite ran before final opt-in hardening; not used as the final fresh-build evidence. |
| `CMAKE_BUILD_PARALLEL_LEVEL=4 flock /tmp/rackpy-host-tests.lock uv run pytest -m "host and not dist" tests/laboratory/test_laboratory.py tests/dsp/test_basic.py --basetemp=build/validation/final -o tmp_path_retention_policy=all` | Pass: final persistent-log repeat, 3 passed in 673.33s (11m13s); pipeline exit 0. Embedded counts are listed below, with only expected mode-specific skips. |
| `uv run pytest -m unit` | Pass: 91 passed, 187 deselected (final repeat: 23.30s). |
| `prek run --all-files` | Pass: all 14 hooks, including Ruff, strict typing, planning data, callback matrix and binding manifest/generated files. |
| `bin/rackpy-dev typecheck` | Pass: strict mypy, 249 source files plus 9 individual CLI checks. |
| `bin/planning-check` | Pass: 45 tasks, 114 dependencies, acyclic; task data agrees. |
| `uv run bin/generate_bindings.py check` | Pass: generated files match `bindings/manifest.json`. |
| `uv run bin/check_coverage.py check --without-built-package` | Pass: absent 2, excluded_internal 111, implemented 2932, unreviewed 13. Built-package Python-name introspection is intentionally not part of this command. |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | Pass: serial build succeeded, 0 warnings, including the final explanation/manifest notes. |
| `uv run pytest -m 'docs and not host' tests/docs` | Pass: 15 passed, 2 deselected in 158.21s. An earlier invocation was interrupted by a 120s tool timeout; the completed rerun used a longer timeout. |

The first completed fresh-build rerun retained all three passing in-Rack
JSON reports, but its outer stdout went to a deleted temporary file and
`/tmp/rp22e-final.log` remained empty after the process exited. Per the
supervisor's recovery instructions, the same standard-harness command was
repeated once under the lock, using `set -o pipefail` and piping combined
output through `tee /tmp/rp22e-final-retry.log`. The recorded
`${PIPESTATUS[0]}` in `/tmp/rp22e-final-retry.exit` was `0`; the complete
header, per-test progress and `3 passed` summary were read back. Retained
final in-Rack JSON reports under `build/validation/final/` each report
exit `0`. No mocked or reused-build fixture plugin was used.

An intermediate `uv run ruff check` identified an unnecessary lambda in a
new test; it was replaced with `seen.append`, and subsequent Ruff/hooks
passed. No failing behavioral test has been hidden or marked xfail.

## Actual-Rack evidence and remaining gates

The normal host harness runs the laboratory suite in the production
plugin at startup and, separately, in the test-hooks plugin with a live
Rack context under Xvfb. The DSP suite runs against the production plugin
and compares the native SDK reference, including RP-18D's MIDI scripts.
The completed fresh-build in-Rack JSON reports give these raw counts:

| Suite | Passed | Skipped | Scope of skips |
| --- | --- | --- | --- |
| Production laboratory at startup | 34 | 5 | Four tests need Rack's live context/test hooks; the new generator Rack-thread probe also needs test hooks. |
| Laboratory with live Rack context (Xvfb, test hooks) | 41 | 1 | `test_parameter_events_and_json_need_racks_context` needs the no-context startup run instead. |
| Production DSP/reference suite | 46 | 0 | None. |
| New MIDI generator tests within production laboratory | 8 | 1 | Rack-thread probe requires test hooks. |
| New MIDI generator tests within live-context laboratory | 9 | 0 | None. |

The new generator suite covers:

- Native/Python differential results for 1, 4 and 16 channels, complete
  bytes/frame/order equality, `super()` recording and retained messages.
- Replacement (no `super()` means no recording), exception propagation
  and a direct superclass recorder call.
- Synchronous generation from a laboratory module's three Bench frames.
- Disabled construction and normal-mode/mixin refusal, plus the native
  opt-in bypass regressions described above.
- Rack-thread refusal with native fallback, and the originally pinned
  reentrant dispatch limitation (now corrected by the follow-up above).

Only Linux x86_64 is exercised. macOS, Windows, Rack Pro, real audio/MIDI
hardware and actual driver-thread scheduling are not tested. The
Rack-context worker probe establishes the refusal guard, not a hardware
or real-time performance claim. No full unbounded deadlock-probe host
suite, distribution/toolchain build, release publication or push was run.
The shared-helper reentry correction is recorded separately above.

## After integration onto RP-23A

The commit was rebased by the supervisor onto `06725f6` (RP-23A), with the
generated files regenerated from the merged manifest (`generate_bindings.py
generate`; `check` and `check_coverage.py check --without-built-package`:
implemented 2942, planned 2, unreviewed 1, excluded_internal 111, absent 2).
An independent read-only review of the change approved it with no findings.
Then: `bin/rackpy-dev build --werror --test-hooks` (CTest 6/6),
`uv run pytest -m unit` (91 passed), `prek run --all-files` (clean), and
`flock /tmp/rackpy-host-tests.lock uv run pytest -m "host and not dist"
tests/laboratory tests/dsp/test_basic.py tests/coverage/test_introspection.py`
(6 passed, 467 s).
