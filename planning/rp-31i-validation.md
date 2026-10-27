# RP-31I validation

The coverage checker now derives and requires 13 provisional branch-specific
records from the lexical declarations in `conditional_blocks`. They are not
bindings or semantic public-API classifications. Their block contents and
skipped variants are fingerprinted; an absent or stale record is a check
failure. Only a successful extraction's `skipped_in` proves absence on that
exact variant. Other platform availability remains unresolved and blocks the
corresponding release. Placeholder records cannot be bound, verified or
excluded until the deferred platform work reconciles them. See
`docs/reference/surface-schema.md#conditional-branch-accountability`.

Linux x86_64, on branch `rp31i` based on RP-31H commit `d19c4e5`:

- `uv run pytest -m unit -q`: 108 passed, 204 deselected. Tests include missing
  branch records, a new lexical declaration, changed branch hash, unresolved
  availability, refused verification, and a missing conditional-block list.
- `uv run bin/rackpy-dev build --werror --test-hooks`: passed, 6/6 CTests.
- `uv run pytest -q tests/coverage/test_introspection.py tests/ui/test_ui.py`:
  4 passed in actual Rack using temporary fixture overrides to the test-hooks
  build, removed afterwards. The built-package coverage audit ran.
- `uv run prek run --all-files`, `uv run bin/rackpy-dev typecheck`,
  `uv run bin/generate_bindings.py check`,
  `uv run bin/check_coverage.py check --without-built-package`, and
  `uv run sphinx-build -W -n -b html docs build/docs/html`: passed.

No macOS or Windows extraction, build or host run was performed; their branch
availability is not inventoried. This is not a cross-platform completeness claim.
