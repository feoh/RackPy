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

**Remaining:** RP-31A is open: the static audit lists 149 relevant entries and
many still require behavioral tests or documentation. Compare every record to
`planning/rp-31-audit/evidence.json`, including RP-31L's prior work; add missing
evidence and validate the complete slice, unit suite, built-package coverage
introspection and remaining host paths before closing. macOS and Windows were not
tested.
