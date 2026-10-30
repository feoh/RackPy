# RP-31 integrated reconciliation evidence

## Integration

The RP-31 family branches were merged into `rp31-integration` based on local
`main` `e5b3743`, in an isolated worktree. `git merge-base --is-ancestor`
confirmed all of `rp31a`, `rp31b`, `rp31c`, `rp31d-followup`, `rp31e`,
`rp31f`, `rp31g`, `rp31h`, `rp31i`, `rp31j`, `rp31k`, `rp31l` and `rp31m`
are ancestors. `rp31d-followup` already includes `rp31m`; `rp31a` includes
`rp31l`; `rp31i` includes `rp31h`. Manifest conflict resolution combined
independent per-record evidence and preserved the more conservative semantic
changes. Registry, mappings and coverage docs were regenerated from the
merged manifest rather than resolved as independent source files. A settings
snapshot type assertion was added after strict mypy found the merge exposed an
`AbstractSet` annotation.

## Linux validation

On Linux x86_64 with the pinned Rack 2.6.6 SDK and Rack Free host:

- Fresh hooks and production builds with warnings as errors each passed all
  seven CTests; the production build covers the merged RP-31J/RP-31M code.
- 156 unit/native tests passed. The separate inventory selection passed 30
  tests, including coverage mutation tests in the unit selection.
- 130 host tests passed under the repository host lock. A separate selection
  of 28 host/introspection/docs tests also passed against the production and
  hooks builds.
- Prek, strict mypy, generated-file check, source-only coverage check,
  `inventory_sdk.py check` (3,248 inventory entries), Sphinx `-W -n`, and
  `git diff --check` passed. Production introspection was exercised by the
  host suite; a source-only coverage check is not a substitute for it.
- Host test fixture overrides pointed to the already-built production and hooks
  directories and were restored after each run.

An independent read-only audit of the integrated tree reviewed the qualitative
scope gaps and found the family work addresses the previously identified
cases. It flagged stale owner-review wording, the open Rack 0 decision, and a
need for integrated production-build evidence. The fresh production build above
addresses the build concern. A separate review of a 41 KB committed-range diff
covering the manually merged source/test conflicts found no issues; it was not
a review of the complete manifest or all generated artifacts. Neither review
ran tests. This is reconciliation evidence, not an independent verification of
every public mapping.

## Decisions and remaining release gates

On 2026-10-30 the owner excluded opt-in `rack0.hpp`, which `rack.hpp` does not
include, from the Rack 2 `rack.hpp` coverage surface. Rack 2 deprecated
symbols reachable from `rack.hpp` remain in scope. The owner also approved
reserving the RackPy PyPI name; the name has not been reserved and approval to
reserve is not a claim that a package was published.

Fresh extraction attempts used the separately verified macOS arm64, macOS
x64 and Windows x64 SDKs. All three returned `not_extracted` because target
C++ standard headers (`cstddef`) were unavailable in this Linux environment.
The Linux SIMDe variant is not a macOS arm64 extraction. Native macOS and
Windows builds, actual physical audio devices, and their host tests remain
unvalidated; cross-platform tasks and release gates must retain that status.
The 13 lexical conditional records remain `unreviewed` pending actual target
inventories. The native MIDI three-byte limit and counted-silence fallback are
recorded in RP-31J; they do not establish full SysEx or rate-transition parity.
No symbol was promoted to `verified` by this integration, and the full release
bar remains unmet.
