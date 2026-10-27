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

**Remaining:** The task is not complete. Add a manifest-driven parametrized
behavioral warning test covering all 92 deprecated/comment-deprecated SDK
entries and a checker rule requiring warning evidence. Several deprecated
no-argument Module callbacks currently share the Python name and signature of
the *modern* event-taking callback; simply warning on that name would warn on
nondeprecated usage. Their actual legacy semantics and mapping need to be
resolved rather than asserting false evidence. Do not promote the records or
close RP-31F until the full task is done.
