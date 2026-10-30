# RP-31K validation (Linux x86_64)

Date 2026-10-27, branch `rp31k` (from `fc4aa6d`). GCC on the host toolchain,
bundled CPython 3.14, pinned Rack Free/SDK 2.6.6. This worktree's own
`build/` only; logs under `build/validation/`. Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`. No push, no Witan calls.

The original checkpoint was not fully validated. The follow-up validation
below reran the fixed test in Rack and checked both production and test-hooks
builds.

Task: `tk-rackpy-31k-settle-template-weak-pointer-and-lock-e4976c`, owner
decisions of 2026-10-27 ([rp-31-slices.md](rp-31-slices.md#decisions)).
Records stay `implemented`. None was promoted to `verified`.

## Done

1. **Accepted instantiation sets.** The notes of the 101 `IIRFilter`,
   `MidiGenerator`, `MidiParser` and `polyDirect`/`polyEstrin`/`polyHorner`
   records now say "owner-approved 2026-10-27" instead of "an owner review
   item". Also updated: `docs/reference/dsp.md`, `docs/coverage.md` policy 10
   (the accepted bounded contracts) and `planning/README.md`. That file's
   "Still open" list now holds only ADR 004's `ui-exclusive` budget, which
   the owner did not decide.
2. **Cheap template gaps.**
   - `math.is_even`/`is_odd`/`is_pow2` use `uint64_t` for 2⁶³ to 2⁶⁴ − 1
     and `int64_t` below; anything outside raises `OverflowError`.
   - `math.sgn(x, type="double")` reaches `sgn<double>`. The default
     `type="float"` keeps the old behaviour; other names raise `ValueError`.
     This API shape was approved by the supervisor.
   - `random.get` takes `int64`/`int32`/`int16`/`int8` (Rack's primary
     template).
   - `native/tests/values_reference.cpp` gained `uint64_t` cases, `sgn_d` and
     `sgn_f_of_d` (including 1e-50), and seeded `random.get` draws of all 11
     types. `test_reference_replay.py` replays them, comparing doubles as 64
     bits.
3. **WeakPtr/WeakBase widget contract and `WeakHandle(void*)`.**
   - The notes cite `weakptr.hpp:64-88`, `:80-83`, `:66-77` and
     `widget/Widget.hpp:21`.
   - New in-Rack test: `test_weak_handle_is_made_by_weak_ptr`.
   - Documented in `docs/reference/services.md#the-widget-contract` and in
     coverage policy 11.
4. **SharedLock over any `TMutex`.**
   - An exact `SharedMutex` keeps the native fast path.
   - Any other object (including a Python subclass) has its `lock_shared()`
     and `unlock_shared()` called with the GIL held and no native lock held.
     An object without both raises `TypeError`.
   - Errors propagate. If `unlock_shared` fails, the lock stays held.
   - A lock dropped while held calls `unlock_shared` only on the thread that
     locked it. On another thread it reports an unraisable error instead;
     during interpreter finalization it only writes a WARN to Rack's log.
   - Five new in-Rack tests. Stub: `_SupportsLockShared`.
5. **Owner-review gate.**
   - Marker: `Open owner review item: <item>`, documented in
     `docs/contributing-bindings.md#open-owner-review-items`.
   - `bin/check_coverage.py release` reports each marked record as an
     `owner-review` problem, even when it is verified.
   - `report` and `docs/reference/coverage.md` list marked records under
     "Needs attention".
   - `check` fails with `owner-review-marker` on any other mention of an
     "owner review item".
   - Unit tests are in `tests/coverage/test_manifest.py`.

## Commands and outcomes

On the committed code (`build/validation/final-*.log`):

| Check | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks` | passed (CTest 6/6) |
| `uv run pytest -m unit` | 112 passed |
| `prek run --all-files` | passed |
| `bin/rackpy-dev typecheck`, `bin/rackpy-dev lint` | passed |
| `bin/planning-check` | ok |
| `bin/generate_bindings.py check` | ok |
| `bin/check_coverage.py check --without-built-package` | ok: implemented 2945, excluded_internal 111, absent 2 |
| `check_coverage report` `open_owner_reviews` | `{}` |
| The check on the pre-RP-31K manifest (`fc4aa6d`) | 101 × `owner-review-marker`, as intended |
| Gate demo (one record marked) | one `owner-review` release problem; listed in the report |

Before the commit, on the same code except one test fix (`*-pre.log`):

| Check | Result |
| --- | --- |
| `uv run pytest -m "native and not host"` | 33 passed |
| `uv run pytest -m "docs and not host"` | 15 passed |
| `sphinx-build -W --keep-going -n -b html docs build/docs/html` | passed, after fixing four cross-doc anchors |
| Host: `tests/values/test_values_in_rack.py`, `tests/services/test_utilities.py`, `tests/services/test_settings.py`, `tests/coverage/test_introspection.py` | 6 passed, 1 failed |

The failure was `test_shared_lock_on_a_python_subclass_of_shared_mutex`, a
test bug: it called `try_lock` on the thread that already held the lock,
which RackPy refuses. The fix probes from another thread and passed in Rack
in the follow-up validation.

## Follow-up validation (2026-10-28)

- Host-toolchain `rackpy-dev build --werror --test-hooks` and the production
  `--no-test-hooks` build each passed 6/6 CTests. The test-hooks build was
  restored afterward.
- With temporary fixture overrides reusing those corresponding builds, host
  tests under `flock /tmp/rackpy-host-tests.lock` passed: settings, utilities,
  test-hooks values and built-package introspection (6 tests); production
  values and built-package introspection (4 tests). The temporary overrides
  were removed. The production introspection checks the manifest against the
  names exported by an actual Rack process.
- `uv run pytest -m unit -q`: 112 passed; `native and not host`: 33 passed;
  `docs and not host`: 15 passed. The native suite initially failed its bundle
  layout test when mistakenly pointed at the test-hooks build; it passed in
  full when rerun against the production build.
- `prek run --all-files`, `rackpy-dev typecheck`, `generate_bindings.py check`,
  `check_coverage.py check --without-built-package` and Sphinx `-W -n` passed.
  macOS and Windows remain untested.

## Follow-up outside this slice

- Open, outside this task:
  - At the time of this review, `docs/coverage.md` policy 11 and
    `native/bindings/dsp/ringbuffer.hpp` retained stale owner-review wording.
    RP-31 reconciliation updated both to record the existing approvals.
  - `rack0.hpp` was an owner review item without a manifest record. On
    2026-10-30 the owner decided to exclude the opt-in Rack 0 shim from the
    Rack 2 `rack.hpp` surface; Rack 2 deprecated declarations reachable from
    `rack.hpp` remain in scope.
- The gate's item text runs to the next `.` or `;`. A marker inside
  parentheses therefore keeps the closing `)` in the item name (seen in the
  demo).
