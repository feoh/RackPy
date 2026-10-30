# RP-31L validation checkpoint

This is a partial checkpoint, not completion of RP-31L. It corrects eight
lifecycle event mapping notes and documents the merged Base64 overload,
`convertFArg` dispatch and weak-pointer assignment semantics. No binding
behavior or coverage status was changed.

On Linux, at this checkpoint:

- `uv run prek run --all-files`: passed.
- `uv run bin/rackpy-dev typecheck`: passed.
- `uv run bin/generate_bindings.py check`: passed.
- `uv run bin/check_coverage.py check --without-built-package`: passed; this
  does not check built-package Python names. Running the check without an
  introspection artifact failed with `missing-runtime-artifact`.
- `uv run pytest -m unit -q`: 106 passed, 203 deselected.
- `uv run sphinx-build -W -n -b html docs build/docs/html`: passed.

## Follow-up checkpoint: container snapshots

Typed the five snapshot properties as read-only `Sequence` or `Set` getters
with whole-container setters, documented copy semantics, updated manifest notes
and added host test assertions for mutations of the returned containers. The
new assertions have not yet run in Rack.

Linux validation in the follow-up worktree:

- `uv run prek run --all-files`: passed after regeneration.
- `uv run bin/rackpy-dev typecheck`: passed.
- `uv run bin/rackpy-dev build --werror --test-hooks`: passed after resuming a
  timed-out first build; CTest 6/6 passed.
- `uv run pytest -m unit -q`: 106 passed, 203 deselected.
- `uv run sphinx-build -W -n -b html docs build/docs/html`: passed.
- `uv run bin/check_coverage.py check --without-built-package`: passed; it
  does not check built-package Python names.
- `uv run pytest -q tests/app/test_app.py tests/services/test_settings.py`:
  timed out during its separate hooks build; no actual-host result obtained.

## Follow-up: actual-host evidence and remaining test gaps

Added live-Rack assertions for all four logging macros and caller locations,
menu helper Python subclasses, the deprecated module-widget constructor inside
its factory (both preview and module), and English translation fallback. The
manifest records retain their existing test and documentation citations and now
note this evidence. No coverage status was promoted.

- `uv run pytest -q tests/app/test_app.py tests/services/test_settings.py tests/ui/test_ui.py tests/services/test_utilities.py tests/app/test_module_browser.py tests/coverage/test_introspection.py`:
  7 passed, 1 failed in 425.60s. The failure was the outer log assertion's
  assumption that Rack strips the path from the caller filename; the host log
  recorded all four correct levels, messages and full paths.
- After correcting that assertion, `uv run pytest -q tests/services/test_utilities.py`
  passed (1 passed in 4.57s), reusing the identical hooks build from the
  preceding run through a temporary fixture override, which was removed.
- The seven passing tests include the app/settings snapshot tests, UI helper
  tests, browser constructor test, and three built-package introspection tests.
  The latter include the complete `check_coverage.py check --introspection`
  invocation against the package running in Rack.

macOS and Windows were not validated. This Linux evidence does not establish
cross-platform support or a full-coverage release.
