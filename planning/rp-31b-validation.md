# RP-31B validation

This checkpoint tests every `rack::event` alias individually in actual Rack for
its exact target identity and DeprecationWarning, and adds a repository unit
test matching the cases to the inventory so omissions fail. UI label and
layout enum values and alias relationships are round-tripped; relevant manifest
records cite these tests. It also documents the menu, scrolling and input
state names listed in the audit. No coverage status was promoted.

Linux x86_64, based on RP-31I/RP-31H: the warnings-as-errors test-hooks build
passed with 6/6 CTests. The actual-Rack UI suite and inventory-completeness unit
test passed with a temporary fixture override, removed afterwards; the UI
suite was rerun after extending enum assertions. The unit suite passed 109
tests. Prek including strict typing, generator checks, non-introspecting
coverage check, and Sphinx warnings-as-errors passed.

## Citation checkpoint

Added the real-frame framebuffer test to the `bypassed` and
`dirty_on_subpixel_change` records, and the live-Rack UI test to the button
and hover-scroll event records and their fields. The former asserts the actual
rendering effect; the latter dispatches real events and asserts their field
values. No bindings or coverage statuses changed.

On Linux x86_64 the following passed in this worktree:

- `bin/rackpy-dev build --werror --test-hooks` (6/6 CTests).
- `uv run pytest -q tests/rendering/test_frames.py tests/ui/test_ui.py` (2 host tests).
- `uv run pytest -q tests/coverage/test_introspection.py` (3 tests, including
  the manifest check against the package running inside Rack).
- `uv run pytest -m unit -q` (109 tests), `prek run --all-files`,
  `bin/rackpy-dev typecheck`, and `uv run bin/generate_bindings.py check`.
- `uv run sphinx-build -q -W -n -b html docs build/docs/html` and
  `uv run pytest -q -m 'docs and not host'` (15 tests).

## Behavioral test checkpoint

Added parameterized tests of five position events on Widget, TransparentWidget,
OpaqueWidget and ZoomWidget, checking child dispatch, scaled coordinates,
propagation and drag-hover consumption. This directly tests the base methods,
unlike the earlier suggested UI recitation. Added drag-origin invalidation,
EventState drag/focus/drop/dirty dispatch, text cursor and clipboard operations,
key and text input, widget drag actions, scroll and scrollbar behavior, list
layout, and SVG file/cache/geometry tests. Manifest entries cite the behavioral
nodes and real-host wrappers; unrelated handler citations were replaced for
those position events. `SVGWidget` currently does not warn: RP-31F was notified.

Linux x86_64: warnings-as-errors test-hooks build and CTests passed as part of
`uv run pytest -q --basetemp /tmp/rackpy-rp31b-tests
 tests/widget/test_tree.py::test_widget_live_ui tests/ui/test_ui.py
 tests/coverage/test_introspection.py`. The initial combined run exposed a bad
expected SVG point count, subsequently corrected. Both widget and UI host suites
were rerun successfully using a temporary fixture pointing to that same build;
the fixture was removed. The introspection artifact from that run passed
`uv run bin/check_coverage.py check --introspection <artifact>` after the final
manifest changes. `prek run --all-files`, `bin/rackpy-dev typecheck`,
`uv run pytest -m unit -q` (109 passed), generator/coverage checks,
`uv run sphinx-build -q -W -n -b html docs build/docs/html`, and
`uv run pytest -q -m 'docs and not host'` (15 passed) also passed.

## Final audit and validation

Added real-host tests for the TextField context menu intercepting input and
all 31 named Widget event handlers dispatching the matching native event type.
The latter checks callback routing, not distinct behavior for Rack's no-op
base methods. Extended the typed-base position tests to hover and button;
replaced unrelated managed/round-trip citations and linked the new SVG and
framebuffer documentation anchors. The original audit entries for enum values
were addressed by the previous `test_fields_round_trip` extension.

After the final edits, `bin/rackpy-dev build --build-dir
/tmp/rackpy-rp31b-tests/hooks-build0 --werror --test-hooks` passed all six
CTests. The widget and UI outer host tests passed against this build with a
temporary `hooks_build` fixture override, removed after the run. The built
package introspection snapshot from the same source was checked again against
the final manifest. Generator check, prek, strict typing, the unit suite
(109 passed), and Sphinx warnings-as-errors passed. The non-host docs suite
(15 passed) passed in the preceding validation round.

The `SVGWidget` alias identity is tested, but its missing deprecation warning
remains a separate RP-31F gap; RP-31F has a checkpoint addressing it on its own
branch and was notified. No entry was promoted to verified. macOS and Windows
were not tested; this does not establish release coverage on either platform.
