# RP-31D checkpoint (incomplete)

This checkpoint adds actual-Rack evidence for CableColorChange on a real cable
(set_cable, fields, redo and undo); independently identifiable plugin and model
metadata fields; the raw random specializations and `u64`; repeated
`SharedLock.release`; and modifier constants. Relevant records cite the tests.
The engine, history, components and random reference pages name additional
previously undocumented operations. No coverage status was promoted.

Linux x86_64, based on RP-31I/RP-31H: the warnings-as-errors test-hooks build
passed with 6/6 CTests. Actual-Rack runs passed for both history tests, the
plugin live-UI test, service utilities, settings, and widget live-UI tests;
temporary fixture overrides reused that build and were removed. `uv run pytest
-m unit -q` passed 108 tests; `prek run --all-files` (including strict typing),
generator check, non-introspecting coverage check and Sphinx
warnings-as-errors passed.

**Remaining:** RP-31D is not complete. Audit every applicable entry in
`planning/rp-31-audit/evidence.json` and reconcile the unresolved WeakHandle
constructor mapping with RP-31K, which is being handled separately. In
particular, finish the remaining cited-test corrections, behavioral tests and
documentation gaps; validate the full slice, including built-package
introspection and relevant host suites, before closing.
macOS and Windows were not tested.
