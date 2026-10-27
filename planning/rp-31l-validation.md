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

Remaining: run those actual-host tests and built-package introspection, and
cover all remaining RP-31L test gaps (logging, menu template subclasses,
deprecated constructor in a factory, translation fallback). macOS and Windows
were not validated. This checkpoint does not satisfy the task's acceptance
criteria.
