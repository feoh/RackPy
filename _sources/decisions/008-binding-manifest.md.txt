# ADR 008: The binding manifest and coverage enforcement

- **Status:** Accepted.
- **Date:** 2026-10-22
- **Task:** RP-11
- **Evidence:** [tests/coverage](../../tests/coverage/test_manifest.py),
  [bindings/manifest.json](../../bindings/manifest.json),
  [docs/reference/coverage.md](../reference/coverage.md)

## Context

The [coverage contract](../coverage.md) defines what complete means. RP-05
produced the denominator. RP-11 makes the contract checkable: a record per
declaration, rules that reject false claims, and generated outputs that cannot
drift from the records.

## Decisions

1. **Records are reviewed data, not generated.** `bindings/manifest.json` has
   one record per counted inventory entry (3,058; imports are not records). It
   is stored one record per line, like the inventory, so reviews see small
   diffs. `bin/generate_bindings.py seed` creates and updates it, and never
   promotes a status.
2. **Statuses** are the six of the contract: `unreviewed`, `planned`,
   `implemented`, `verified`, `unrepresentable`, `excluded_internal`. Only
   `verified` and `excluded_internal` satisfy the release bar.
3. **Fingerprints** hash each inventory entry without its line number. A
   changed declaration makes its record stale; `seed --update` resets reviewed
   ones to `unreviewed` with a note. A changed inventory file is detected from
   its SHA-256 in the manifest header.
4. **Exclusions are an explicit allowlist** (`bindings/exclusions.json`): 111
   entries, each the inventory's own evidence (`PRIVATE`, 69; incomplete pimpl
   `Internal` type, 42) and its policy number (2 or 3). An allowlist entry for a
   declaration the inventory does not mark internal is rejected.
5. **Derived fields follow their sources.** `callback` comes from
   `upstream/callback-rules.json` through `bin/callback-matrix`; `locks_engine`
   from the new `upstream/engine-locks.json`, which records the lock of all 52
   public `Engine` methods with line evidence in the pinned `Engine.cpp`:
   15 exclusive, 15 shared, 18 none, and 4 `_NoLock` variants that need the
   caller to hold the lock already.
6. **A bound record must cite evidence the check can follow:** an
   implementation file, test node IDs that exist in the test files, docs
   anchors that exist on the pages, and Python names present in an
   introspection of the **built package**, taken inside Rack
   (`rackpy._introspect`). Without that artifact, a record claiming a bound name
   fails. A facade function that raises `NotImplementedError` and an object
   documenting itself as not implemented never count. An alias counts only
   when its target is bound. An `unresolved` callback cannot be planned.
7. **The GIL rule is enforced twice.** A mapping of an `exclusive` or `shared`
   Engine method without `gil_release: true` fails the check, and
   `generate_bindings.py` refuses to generate from it (ADR 003, decision 2).
8. **Generated files** (the C++ → Python map, a registration scaffold, stubs,
   the coverage report) carry a digest of their inputs. `generate_bindings.py
   check` regenerates them in memory and fails on any difference, so a hand
   edit or a forgotten regeneration fails CI. The registration scaffold calls
   one hand-written adapter per family (`native/bindings/<family>.cpp`) and is
   compiled into the host; generation fails if a family with bound records has
   no adapter.
9. **Reports** count every declaration once, by family, platform and status.
   Platforms with no extracted inventory (macOS and Windows today) are shown as
   "not inventoried" and fail the release check; unknown, unrepresentable and
   unresolved entries are listed first.
10. **Failure output** leads with a count per problem kind and shows the
    rarest kinds first, so a single missing artifact is not hidden under
    thousands of incomplete records.

## Consequences

- RP-11 binds nothing: every record is `unreviewed` or `excluded_internal`, and
  `check_coverage.py release lin-x64` fails by design.
- Each family task updates records and adapters as described in
  [Adding bindings](../contributing-bindings.md).
- The surface tables in the reference show each declaration's manifest status.
- 31 virtual methods have an `unresolved` callback strategy (audio and MIDI
  drivers); RP-29 must resolve them before they can be planned.
