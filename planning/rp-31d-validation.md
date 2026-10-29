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

## Integrated follow-up

RP-31K's owner-approved `WeakHandle(void*)` disposition is now integrated from
main. On the rebased tree, the test-hooks build passed 6/6 CTests. Actual-Rack
history, plugin metadata, settings, utilities, values, live engine-module and
built-package introspection tests passed (10 outer tests); temporary fixture
overrides reused that build and were removed. Unit tests: 114 passed. The
generator check, non-introspecting coverage check, prek and Sphinx `-W -n`
passed. The production build also passed 6/6 CTests, production values and
built-package introspection passed (4 outer tests), the non-host native suite
passed 33 tests, and the non-host docs suite passed 15. Production and
host runs reused their corresponding builds with temporary fixture overrides;
the test-hooks build was restored afterward. No macOS or Windows run was made.

**Still open:** complete the per-record audit before closing. In particular,
`plugin::Plugin::~Plugin()` cites a test of the plugin `destroy()` hook, not
an observation of `~Plugin`. Rack 2.6.6 `src/plugin/Plugin.cpp:14-20` clears
`model->plugin` for each attached model without deleting the models. RackPy's
`PluginHandle` is borrowed (`native/bindings/plugin.cpp`), so the cited Python
test cannot exercise that effect. Determine a defensible destructor proof and
mapping instead of treating the hook as evidence. The record is now `planned`;
RP-31M is responsible and blocks RP-31D. Re-run applicable checks on the final
changes.

## Random-reference follow-up (incomplete)

On RP-31M's `85320d6` descendant of the RP-31D checkpoint, the Rack C++
reference replay now checks the named random functions against seeded
`get<T>` draws, in addition to comparing those draws with the compiled Rack
reference. This catches swapped named bindings that simple range checks miss.
The seven affected manifest records cite the replay; no status was promoted.
The remaining per-record audit has not been completed.

Linux x86_64: a fresh warnings-as-errors test-hooks build passed 6/6 CTests
(the first build command timed out partway; resuming it completed). The
actual-Rack test-hooks values suite passed, using temporary fixture overrides
for the fresh build under `/tmp/rackpy-host-tests.lock`; the overrides were
restored. `pytest -m unit` passed 114 tests; prek, strict typecheck, generated
file check and Sphinx `-W -n` passed. Production build and production-host
replay were not rerun for this follow-up. macOS and Windows were not tested.
