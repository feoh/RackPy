# RP-25B local validation

Date: 2026-10-23. Branch: `rp-25-continuation`, based on
`origin/rp-25-widget-foundations` at `fcceef8`. Task:
`tk-rackpy-25b-checked-widget-geometry-and-spatial-q-8df27f`.
Scope and remaining gates: [RP-25 slices](rp-25-slices.md).

## Commands and results

| Command | Result |
| --- | --- |
| `uv sync --frozen` | Fresh worktree environment installed |
| `bin/rackpy-dev build --werror --build-dir build/dev --test-hooks` | Warning-as-error host build and 4/4 native CTests passed, including SDK geometry/coordinate/viewport differential checks |
| `flock /tmp/rackpy-host-tests.lock uv run pytest -m 'host and not dist' tests/widget/test_tree.py -ra` | 2 outer actual-Rack tests passed; inner live-UI tests exercised geometry, visibility, ancestry, coordinates, viewport, wrong-context and stale-reference cases |
| `flock /tmp/rackpy-host-tests.lock uv run pytest -m 'host and not dist' tests/coverage/test_introspection.py -ra` | 3 actual-host introspection/coverage tests passed against the built package |
| `prek run --all-files` | All hooks passed, including Ruff, strict mypy, planning, callback matrix and binding-manifest checks |
| `bin/planning-check` | 45 original planning tasks / 114 dependencies remained acyclic and synchronized |
| `bin/generate_bindings.py check` | Generated manifest mappings, registry scaffold and coverage report match |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | Documentation built without warnings |
| `git diff --check` | Passed |

The broader non-dist/non-inventory regression was run before the final change
that restored the raw SDK's required ancestor argument (169 passed, 31
platform/toolchain gates deselected). Final targeted widget and introspection
host suites passed after that signature correction. Dist, sanitizer, external
link, macOS and Windows gates were not run. No graphics, event callback,
subclass, scene/window integration or performance claims follow from this slice.

## Coverage boundary

Exactly the 12 RP-25B `rack::widget::Widget` geometry/spatial method records
were mapped to the checked `WidgetNode` adapter and remain **planned**. Generated
coverage and raw stubs were refreshed. No Widget entry is claimed complete.
RP-25A remains the only detached-tree ownership groundwork; full class factories,
mutable raw fields, ZoomWidget/TransformWidget, event dispatch, callbacks and
scene/browser/window teardown remain parent-task acceptance gates. The project
remains in implementation.
