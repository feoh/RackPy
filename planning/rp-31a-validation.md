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

Further actual-Rack evidence: `uv run pytest -q tests/app/test_app.py`
passed with selection delta movement, nearest-grid movement, finite-value
rejection and re-selection from Rack's active drag rectangle. The app
reference now names selection and container queries. `uv run pytest -q
 tests/app/test_module_browser.py` passed with assertions against the hovered
input and output plug of a cable during a real drag. An attempted reverse drag
after cloning was not reliable in the existing fixture and was removed. A
subsequent run starts the input-first drag before cloning, and
`uv run pytest -q tests/app/test_module_browser.py` passed with non-null
hovered-output evidence and `clear_cables()` removal without a history entry.

Tooltip evidence: `uv run pytest -q tests/app/test_app.py` rebuilt with test
hooks and passed. A test-only helper counts public `ui::Tooltip` instances in
Rack's scene; actual Rack tests verify create/destroy, idempotent creation and
enter/leave for param, port and module-light controls. The twelve corresponding
manifest entries cite this test and the reference documents tooltip behavior.
No private Rack API is accessed.

Menu-action evidence: `uv run pytest -q tests/app/test_app.py` passed after
rebuilding the test hooks. A test-only helper inspects and dismisses the Rack
menu overlay through public widget types. It checks ten audio/MIDI choice and
button actions against their actual menu labels. Rack 2.6.6's combined MIDI
menu omits the channel; the first test expectation incorrectly included it and
was corrected against `src/app/MidiDisplay.cpp`. All ten manifest records cite
the new test.

`uv run pytest -q tests/app/test_app.py` also passed with selection bypass
and undo checked against Rack's history, and a knob scroll/leave test proving
Rack records an undoable parameter change. Settings are restored after the
knob test.

`uv run pytest -q tests/app/test_module_browser.py` passed with live cable
removal, undo and reconnection checks for `delete_top_cable_action()`,
`clear_cables_action()` and `clear_cables_on_port()`. It also verifies that
`reset_selection_action()` resets the selected parameter and Rack history
undo restores the prior value.

`uv run pytest -q tests/app/test_app.py` passed with all five colored
`LEDSlider` classes checked against their respective light specializations and
colors; the five manifest entries cite the new behavioral test.

`uv run pytest -q tests/app/test_app.py` passed with `SvgKnob`, `SvgSlider`
and `SvgSwitch` change handlers checked against their native child transform,
handle position and SVG frame at different parameter values. Their manifest
records cite the new test.

`uv run pytest -q tests/app/test_app.py` passed with an `SvgButton` subclass
checking that mouse-down dispatches one action and drag-drop does not dispatch a
second. The drag-drop record cites the actual-host test.

`uv run pytest -q tests/app/test_module_browser.py` passed with preset
`load_action()` and history undo, plus selection file save/load and undo.
The selection-load test counts Rack's actual module widgets: the loaded
widget's factory sees an initial module ID of -1, so the fixture's
ID-indexed `VOICES` dictionary cannot count it reliably at construction time.

`uv run pytest -q tests/app/test_module_browser.py` passed with selection
clipboard copy/paste, disconnect and clone actions and history undo, checked
against Rack's widget and cable counts. Rack's clipboard paste action returns
`None`; its behavior is checked through the added module instead.

`uv run pytest -q tests/app/test_app.py` passed with exact centered slider
positions, the legacy SVG setters' warning and write-through effects, both
SVGs on themed screw/panel, and Rack's module-position squeeze behavior.

`uv run pytest -q tests/app/test_app.py` passed with direct construction of
`RoundKnob`, `Davies1900hKnob` and `Rogan`, their angle limits and native
background/foreground child layers; their constructor records cite the test.

`uv run pytest -q tests/app/test_app.py` passed with Rack's contrasting
parameter and switch double-click behavior tested against actual values;
the two records cite that test.

`uv run pytest -q tests/app/test_module_browser.py` passed with selection
randomize/undo/redo, deletion/undo, and module JSON paste/undo checked in
actual Rack. Randomization retries up to three times if Rack happens to pick
the existing parameter value.

A continuation added direct, actual-Rack behavioral tests for
`Knob.on_drag_move` (right button ignored, left linear drag changes the
parameter) and `Knob.on_action` (a short click calls a Python override and
its Rack base; a 20-pixel drag does not). The relevant manifest records cite
them and the app reference explains the behavior. The Linux test-hooks build
with `--werror` and 6/6 CTests passed, and `tests/app/test_app.py` passed using
a temporary fixture pointing to that build; the fixture was restored.

**Remaining:** RP-31A is open: the static audit lists 149 relevant entries and
many still require behavioral tests or documentation. Compare every record to
`planning/rp-31-audit/evidence.json`, including RP-31L's prior work; add missing
evidence and validate the complete slice, unit suite, built-package coverage
introspection and remaining host paths before closing. macOS and Windows were not
tested.
