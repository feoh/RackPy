# RP-38 validation: concurrency, security, lifetime and failure recovery

Task: `tk-rackpy-38-harden-concurrency-security-lifetime-a-eb9bbc`, branch `rp38`.
Machine: Intel i9-14900K, Linux 7.3.0-4-cachyos x86_64, GCC 16.2.1, Python 3.14,
Rack Free 2.6.6 (`061ccf63c1758599396ac1bb10d47345d9d34076`), Mesa llvmpipe under Xvfb.

## What was delivered

- **Sanitizer builds.** `RACKPY_SANITIZE` (`address,undefined` or `thread`) and
  `bin/rackpy-dev configure|build --sanitize` instrument every native target.
  `tests/hardening/test_sanitizers.py` (`-m sanitizer`, opt-in) builds both
  from scratch, runs the native tests under each, and runs twelve live-Rack
  suites (the fuzzers and the stress suite included) on the ASan+UBSan plugin
  inside actual Rack with `libasan` preloaded. A deliberate heap overflow
  (`_testing.sanitizer_canary`, test-hooks builds only) must be reported first,
  proving ASan is active in Rack.
- **Fuzzing** (`tests/hardening/in_rack_fuzz`, seeded and bounded): mutated
  graph plans checked natively (and run when accepted), JSON conversion,
  indices and counts, values returned by Python overrides, every method of
  dead handles, and a widget deallocated while Rack asks it questions.
- **Stress** (`tests/hardening/in_rack_stress`, `-m stress`, opt-in): 1,000
  create/delete and 1,000 save/reload cycles; 4,000 random main-thread
  operations racing Rack's engine (modules, cables, parameters, kernel controls
  and replacement, sample rates, reset, randomize, bypass, saves, state
  replacement, menus, expander hot-removal); slow and raising Python callbacks
  against the engine's measured idle rate; 5,000 raises for bounded logging;
  shutdown with everything left live. The RP-07 audio guard is preloaded and
  now marks RackPy's own `process()`/`processBypass()` in test-hooks builds; a
  kernel that allocates every frame is its positive control.
- **Security** (`tests/hardening/test_security.py`, production bundle): Python
  environment variables, `.pth`/`sitecustomize`/`usercustomize` files, plugin
  files named like the standard library, patch data, patch keys and patch
  storage never run code (each would leave a marker); ten malformed loader
  configurations and a symlink out of the plugin are refused.
- **Files removed under a running plugin** (`tests/hardening/test_unload.py`).
- `SECURITY.md` and `docs/explanation/security.md`.

## Findings, all fixed with regression tests

| Finding | Severity | Fix | Regression test |
| --- | --- | --- | --- |
| A widget deallocated by Python (refcount 0) was resurrected when a child asked for its parent, and pybind11 then deallocated it twice ("Tried to deallocate unregistered instance") | High: memory corruption reachable from correct API use | `managed.cpp` `wrapper()` treats a refcount-0 peer as gone and lends the live SDK base | `in_rack_fuzz/test_lifetime.py`; `test_fuzz.py` checks Rack's log |
| The plugin's `pythonpath` came before the standard library: a plugin file named `json.py` replaced `json` for RackPy and every import | Medium | `pythonpath` follows the standard library; start-up names shadowed files | `test_plugin_files_never_replace_the_standard_library` |
| A Python callback raising every frame wrote a log line every time | Medium (unbounded disk) | first 20, then at most one a second, with a one-time notice and the omitted count | `test_stress.py` (5,000 raises leave 20 entries) |
| A host library or `pythonpath` symlinked outside the plugin was followed | Low (trusted plugin) | loader refuses it, as kernel libraries already were | `test_symlinks_out_of_the_plugin_are_refused` |
| `Module.data_from_json`, called by Rack under its exclusive engine lock, was not timed: a slow one was not diagnosed | Low | `timedExclusive` records `slow_exclusive_callback` | `test_slow_and_failing_python_never_stalls_the_engine` |
| A failed explicit `py::cast` (a plan's op, an event field) raised `RuntimeError` | Low | translated to `TypeError` | `test_wrong_types_at_explicit_casts_are_type_errors` |
| `download()` left Rack's download running, writing the file and holding up the interpreter's exit, when `on_progress` raised (RP-32 review) | Low | the download runs to its end, then the error propagates; Rack's API cannot cancel | `test_a_failing_progress_callback_leaves_no_download_running` |

RP-22F follow-ups (task comment): (1) the laboratory under ASan+UBSan is in
the sanitizer suites; (2) the refcount-0 rule for widgets is
`test_lifetime.py`; (3) the keyboard route's delivery depth around drained
releases is
`test_releases_drained_after_a_keyboard_delivery_still_count_as_inside_it`;
(4) Python-made ports dropped on a non-main thread are still leaked with a
warning, unchanged and documented in `docs/explanation/security.md`.

Not a finding, confirmed and documented: Rack calls `fromJson`, reset,
randomize and bypass with its engine lock held exclusively, and RackPy runs
Python handlers there as Rack runs C++ ones, so a slow handler stops audio
(ADR 004). Each slow one is diagnosed.

## Evidence

### Stress (`-m stress`, build/dev with `--werror --test-hooks`, audio guard preloaded)

7 in-Rack tests passed. Raw numbers from the run:

- Create/delete, cycle 300 versus cycle 1,000 (Rack's history is full at 250):
  `native_references` 1270/1270, `transferred_objects` 1256/1256,
  `pending_releases` 11/11, `identity_entries` 9/9, engine modules 2/2, Python
  objects 81621/81621, RSS 227,741,696/227,741,696 bytes.
- Save/reload, the same: 1253/1253, 1249/1249, 11/11, 9/9, 1/1, 81613/81613,
  227,741,696/227,741,696 bytes. Every reload restored state and parameter.
- Race: 4,000 operations, every kind used (add 262, bypass 254, connect 266,
  control 256, delete 224, disconnect 264, expander 291, kernel 272, menu 252,
  param 269, randomize 264, rate 285, reset 313, save 268, state 260); no
  callback failed; every kernel still advancing afterwards.
- Engine frames per second (fallback engine thread at 48 kHz): idle 47,977;
  while 18 callbacks outside the exclusive lock slept 20 ms each (0.37 s)
  48,104; while 30 inside it did 35,642, with 30 `slow_exclusive_callback`
  diagnostics; while 42,384 callbacks raised (0.50 s) 49,466.
- Audio guard inside RackPy's `process()` during the hostile phases: malloc 0,
  free 0, calloc 0, realloc 0, aligned 0, mutex_lock 0, write 0, gil_ensure 0.
  Positive control (`rackpy_test.Allocate`): over 1,000 malloc and free in 0.1 s.
- 5,000 raises from `Widget::step` plus the hostile phases: 20 log entries and
  the one-time notice. Rack quit cleanly with eight modules, expanders, a menu
  and queued controls live.

### Fuzzing (build/dev)

7 in-Rack tests passed: 600 graph plans (172 compiled and ran 64 frames, 428
refused), 1,500 JSON values (563 round-tripped exactly, 937 refused), 3,000
index operations and 200 configurations, 300 hostile widgets and eight child
menus, more than 100 calls on dead handles. Only `Module.create` (static) and
the expander views (which refuse on use, asserted) answered on a dead module.

### Security (build/production)

14 passed: environment, standard-library shadowing, patches never run code, ten
malformed loader configurations, symlink refusal.

### Sanitizers

To be filled from the final `-m sanitizer` run.

### Full suite

To be filled.

## Limits and blockers

- ThreadSanitizer runs on the native tests only: Rack and CPython are not
  instrumented, so it cannot run in Rack's process.
- The sanitizers do not cover Rack or CPython.
- Linux x86_64 only. macOS and Windows are not built (RP-40, RP-41).
- Physical audio devices are untested; the engine ran on Rack's fallback thread.
- GitHub private vulnerability reporting is **disabled** on `feoh/rackpy`
  (`gh api repos/feoh/rackpy/private-vulnerability-reporting`:
  `{"enabled":false}`). `SECURITY.md` says so. Enabling it is an owner action,
  required before the first release (RP-44).
