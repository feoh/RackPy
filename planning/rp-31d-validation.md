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

## 2026-10-28 additional checkpoint

Added an actual-Rack test for `Quantity.set_display_value`, including Rack's
parser dispatching to a Python override and `super()`; added individual macro
level/caller assertions, including DEBUG even when Rack filters debug output.
The manifest now cites those tests and the existing metadata tests that actually
exercise `LightInfo.description`, `PortInfo.description` and `PortInfo.port_id`.
No coverage status was promoted.

On Linux x86_64: `CMAKE_BUILD_PARALLEL_LEVEL=2 uv run python bin/rackpy-dev
build --werror --test-hooks` passed 6/6 CTests; targeted Rack tests
`tests/services/test_utilities.py` and the test-hooks case in
`tests/values/test_values_in_rack.py` passed using temporary fixture overrides
pointing at that build (removed afterward). `uv run pytest -m unit -q` passed
108; `prek run --all-files`, `rackpy-dev typecheck`, generator check,
non-introspecting coverage check and Sphinx warnings-as-errors passed. These
checks preceded the final small macro-test extension; re-run them on the final
tree. Built-package introspection and other host suites remain to be run.

The `WeakHandle(void*)` disposition belongs to RP-31K's separate WIP branch;
do not close RP-31D until it is reconciled. Complete the remaining audit and
full validation on the integrated tree.
