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

Remaining: container copy typing/documentation and all specified test gaps;
build with warnings as errors and test hooks, full coverage with built-package
introspection, and targeted actual-Rack host tests. macOS and Windows were not
validated. This checkpoint does not satisfy the task's acceptance criteria.
