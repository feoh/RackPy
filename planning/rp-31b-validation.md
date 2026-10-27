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

**Remaining:** RP-31B is open. The audit has many other widget, event, window
and UI evidence gaps, including unrelated widget handler citations, drag/drop,
text editing, and SVG operations. Complete the audit, check all behavioral
paths and citations, and rerun validation including built-package coverage and
relevant host tests before closing. macOS and Windows were not tested.
