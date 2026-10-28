# RP-31A checkpoint (incomplete)

This checkpoint builds on RP-31L and adds actual-Rack tests for the remaining
SVG aliases, several light template specializations and aliases, and the ordered
and distinct child views of audio and MIDI displays. It documents the display
fields, themed SVGs, slider fields and legacy setters, and rack-scroll child
views; the relevant manifest records cite the evidence. No coverage status was
promoted.

Linux x86_64: `uv run bin/rackpy-dev build --werror --test-hooks` passed with
6/6 CTests. `uv run pytest -q tests/app/test_app.py` passed against actual Rack
with a temporary fixture override to that build; it was rerun after the display
assertions. `uv run pytest -m unit -q` passed 106 tests. `uv run prek run
--all-files` (including strict typing), generator check, non-introspecting
coverage check and Sphinx warnings-as-errors passed. Temporary fixtures were removed.

A later documentation-only checkpoint corrected the undo distinction between
`ModuleWidget.disconnect()` and `disconnect_action()` against their native
binding wrappers. In the RP-31A worktree, `uv run prek run --all-files`
(including strict mypy) and `uv run sphinx-build -W -n -b html docs
build/docs/html` passed. At that checkpoint no new host behavior test was run.

Additional Linux actual-host evidence: `uv run pytest -q
 tests/app/test_module_browser.py` passed (1 test), including cable removal by
`disconnect()` with unchanged history index. `uv run pytest -q
 tests/app/test_app.py` passed (1 outer test), including exact pinned SDK RGBA
values for all component scheme constants and cable colour ID round-tripping.
The scheme records now cite the behavioral test; no status was promoted.
`uv run bin/generate_bindings.py generate`, `uv run prek run --all-files`,
`uv run pytest -m unit -q` (106 passed), and `uv run sphinx-build -q -W -n
-b html docs build/docs/html` passed after these changes.

**Remaining:** RP-31A is open: the static audit lists 149 relevant entries and
many still require behavioral tests or documentation. Compare every record to
`planning/rp-31-audit/evidence.json`, including RP-31L's prior work; add missing
evidence and validate the complete slice, unit suite, built-package coverage
introspection and remaining host paths before closing. macOS and Windows were not
tested.
