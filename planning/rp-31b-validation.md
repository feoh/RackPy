# RP-31B checkpoint (incomplete)

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

**Remaining:** RP-31B is open. The audit has many other widget, event, window
and UI evidence gaps, including unrelated widget handler citations, drag/drop,
text editing, and SVG operations. The audit's proposed recitation of
`OpaqueWidget::onHoverKey`, `onHoverText`, and `onHoverScroll` to the UI test
needs additional evidence: `Pad` overrides those handlers without calling
`super()`, so that test alone does not exercise the base implementations.
Complete the audit and rerun validation before closing. macOS and Windows were
not tested.
