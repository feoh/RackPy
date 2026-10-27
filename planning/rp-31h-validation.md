# RP-31H validation

Policy 13 already calls for event boundary constants: this slice counts the
143 reached GLFW key, mouse-button, modifier and action constants and ten
Blendish corner flags, not the general dependency APIs. Inventory entries are
keyed by `boundary_constant:<name>` and are seeded without changing existing
review statuses. Values are bound from the included SDK headers; generated
stubs and C++ registrations follow the inventory. No mapping is promoted to
`verified`.

Linux x86_64, Rack Free 2.6.6:

- `uv run bin/inventory_sdk.py check`: 3248 entries match, including 153 new
  boundary entries; macOS/Windows extraction attempts remain unavailable.
- `uv run pytest -q tests/inventory`: 30 passed, including a miniature fixture
  checking reached dependency headers, aliases, nested `extern "C"`, and
  exclusion of unrelated dependency constants.
- `uv run bin/rackpy-dev build --werror --test-hooks`: passed, CTest 6/6.
- `uv run pytest -q tests/ui/test_ui.py tests/app/test_app.py tests/coverage/test_introspection.py`:
  5 passed. A temporary fixture override reused the werror/test-hooks build and
  was removed afterwards. The UI suite compiles an independent `rack.hpp`
  reference and compares all 153 values inside real Rack, and checks assigning
  a combined Blendish flag to a menu. The introspection suite checks the
  manifest against the built package.
- The first UI attempt failed because the independent reference executable
  omitted `-lRack`; after adding the pinned SDK library and rpath, the UI
  suite passed and was rerun in the five-test command above.
- `uv run pytest -m unit -q`: 106 passed, 204 deselected.
- `uv run prek run --all-files`, `uv run bin/rackpy-dev typecheck`, and
  `uv run sphinx-build -W -n -b html docs build/docs/html`: passed.

macOS and Windows were not built or run. The inventory and tests above do not
establish cross-platform support or a full-coverage release.
