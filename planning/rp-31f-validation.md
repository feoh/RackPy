# RP-31F checkpoint (incomplete)

This checkpoint adds warnings for the seven previously silent spellings:
`ui.ScrollBar`, `widget.SVGWidget`, `LEDLightButton[...]`, construction of
`LEDSliderGreen`, reading/writing `Scene.mouse_pos` and
`RackWidget.touched_param`, and defining/calling `Widget.draw_vg`. Records,
stubs and docs were updated. Rack's normal internal legacy draw dispatch does
not warn per frame; the Python subclass definition and explicit base call do.

Linux x86_64: `uv run bin/rackpy-dev build --werror --test-hooks` passed,
including 6/6 CTests. Actual-Rack app and UI suites and the widget live-UI
suite passed (3 outer tests) with temporary fixture overrides to that build;
the widget live-UI suite was rerun after adding the explicit draw warning
assertion and passed. `uv run pytest -m unit -q` passed 108 tests;
`prek run --all-files`, generator check, non-introspecting coverage check,
strict typing via prek and Sphinx warnings-as-errors passed. A first combined
host run timed out because it also triggered a separate production build; its
orphaned compiler was terminated and temporary fixtures were removed. Built
package coverage introspection initially found the new lazy aliases missing; the
module export lists were fixed and its three tests then passed using a temporary
fixture override. Non-Linux validation was not run.

## Second checkpoint (incomplete)

Added an inventory-driven actual-Rack matrix for all 92 deprecated declarations:
45 alias spellings and 47 operations. Five additional cases assert the modern
Module event overloads do not warn. The outer test checks the inventory and
reported parametrized test count. Added `bindings/deprecation-evidence.json` and
a checker rule requiring every bound deprecated record to cite its listed
warning test, with mutation tests. The five legacy no-argument Module virtuals
are now `planned`, not `implemented`: explicit no-argument calls warn, but
Python cannot independently override these virtuals and reproduce Rack's
legacy versus modern override and superclass dispatch. Their records name
RP-31F as responsible; the parent task has been notified. No coverage status
was promoted.

Linux x86_64: test-hook build with warnings as errors and 6/6 CTests passed;
the actual-Rack warning matrix passed after the status changes (outer test,
including its per-case assertions); 109 unit tests and `prek run --all-files`
passed. Generator and source-only coverage checks passed (3093 implemented,
5 planned); built-package introspection in Rack passed 3 tests on the changed
records. Strict typing and Sphinx warnings-as-errors passed, and 15 docs tests
passed before the final record-only status changes. Temporary test fixture
changes were removed. macOS and Windows were not tested.

**Remaining:** Implement and test the five independently overridable legacy
virtuals without warning on modern callbacks, or otherwise resolve their full
public semantics. Re-run relevant final validation and do not close RP-31F or
claim full coverage until the gap is resolved.

## 2026-10-28 investigation

Corrected `ModuleClass`'s docstring: the legacy events do not yet have their
own Python override. Rack's `Module.hpp:376-379` and `:407-410` dispatch the
legacy no-argument virtual from the default modern event implementation;
`Reset` and `Randomize` are out-of-line. `native/laboratory/module.cpp`
currently registers only the modern callbacks, while `module::Module` calls
Rack's modern implementation before recording a snapshot. A candidate fix
needs distinct legacy override slots and explicit base methods, preserving
modern override suppression and `super()` reentry without Python on a
production/audio thread. Validate every path in real Rack, including warnings
for defining/calling legacy methods and no warnings for modern ones. The
candidate remained unimplemented at that checkpoint; the five records remained `planned`.

## 2026-10-28 continuation

Implemented separate laboratory override slots for the five no-argument
virtuals. Each base method calls Rack's qualified no-argument implementation;
Rack's modern default event can dispatch the independent legacy override,
while a modern override that omits `super()` suppresses it. Defining an
old override and explicit deprecated base calls warn; implicit calls made by
Rack's modern handler do not. Raw direct calls still warn. The five records
are `implemented`, not `verified`.

A live-Rack regression test exposed swallowed callback exceptions when a
laboratory module's event was directly dispatched through its raw Module
handle. That path now keeps the GIL and captures callback errors for the
Python caller, only for laboratory modules; the production path remains
unchanged and releases the GIL. Actual Rack tests cover all five virtuals,
definition and explicit-call warnings, modern dispatch, suppression and
super dispatch, and direct exception propagation.

Linux x86_64: `CMAKE_BUILD_PARALLEL_LEVEL=2 uv run bin/rackpy-dev build
--werror --test-hooks` passed with 6/6 CTests; the actual-Rack live-context
laboratory suite, the new legacy event suite and the live-context module
suite passed. The inventory-driven warning matrix and built-package introspection passed in actual Rack using
the test-hooks build. `uv run pytest -m unit -q` passed 109 tests;
`uv run prek run --all-files` passed, including strict mypy, generator and
source coverage checks; `uv run sphinx-build -W -n -b html docs
build/docs/html` passed. `uv run pytest -m docs -q` passed 17 tests with a
temporary test fixture selecting the existing test-hooks build, removed
afterward. An earlier docs run timed out building a separate production
host; its orphaned compiler processes were terminated. Non-Linux platforms
and a fresh production build were not tested in this continuation.
