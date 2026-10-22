# RP-25A local validation

Date: 2026-10-22. Branch: `rp-25-widget-foundations`, based on `8c12ff6`.
Task: `tk-rackpy-25a-checked-detached-native-widget-tree-o-79a09d`.
Scope and remaining gates: [RP-25 slices](rp-25-slices.md).

## Environment

Linux x86_64; GCC 16.2.1 (20260810); uv 0.9.18; the checksum-verified
Rack SDK and Rack Free 2.6.6; bundled CPython 3.14.9; pybind11 3.1.0.
GUI tests use Xvfb/Mesa, a temporary Rack user directory and no audio device.
Actual-host runs were serialized with `flock /tmp/rackpy-host-tests.lock`.
No performance claim follows from these tests.

## Commands and observed results

| Command | Result |
| --- | --- |
| `uv sync --frozen` | Fresh worktree environment installed successfully |
| `bin/rackpy-dev build --werror --build-dir build/dev` | Production plugin compiled; 4 CTests passed |
| `cmake --build build/dev --parallel 4 && ctest --test-dir build/dev --output-on-failure` | Final native allocator-failure and shutdown-race additions compiled; 4/4 CTests passed |
| `prek run --all-files` | All hooks passed, including strict typing, planning, callback matrix, manifest and generated outputs |
| `bin/rackpy-dev typecheck` | Strict mypy passed for src/tests/docs/tools and all eight extensionless utilities |
| `flock /tmp/rackpy-host-tests.lock uv run pytest -m 'not dist and not inventory' -q` | 169 passed, 31 deselected, 227.24 s |
| `uv run pytest -m inventory -q` | 29 passed, 171 deselected, 8.00 s |
| `bin/check_coverage.py check --introspection <actual-Rack artifact>` | Passed against the production package introspection emitted by tests/coverage |
| `bin/generate_bindings.py check` | Generated files match the manifest |
| `uv run sphinx-build -W -n -b html docs build/docs/html` | Passed with warnings treated as errors |
| `git diff --check` | Passed |

The full regression includes production startup refusal (one in-Rack test)
and live UI execution (ten in-Rack tests, 2.76 s) from
`tests/widget/test_tree.py`. The UI suite verifies actual-host shutdown via
the `detached-widget-cleanup-ok` marker, retained subtree invalidation and
zero native roots at plugin destroy, plus context/thread refusal, readonly
ownership fields, wrapper identity, add/remove/reparent/clear, copied layout
and snapshots, the live-UI documentation exercise, and 1,000 owner churns
without native-root-count growth. The test-only probe/statistic is absent
from production builds.

Native `widget_tree` also compares native SDK child order/bounds and tests
real SDK list-allocation failure (including rollback of its early parent
assignment), worker final release and 100 worker-release/shutdown races.
Native tests do not substitute for the separate actual-Rack runs.

## Coverage and deferred gates

The inventory denominator remains 3,058. Eighteen Widget records moved
from unreviewed to **planned**, not implemented/verified; no full public
Widget mapping is claimed. The full-release check still intentionally fails.

Not run locally: the two `dist` tests using the official distribution
toolchain, sanitizer builds, external link checking, macOS or Windows.
The change does not establish scene/browser integration, drawing, transformed
widgets, Python lifecycle/event callbacks or audio-driver behavior.
Remote CI and review are separate gates; this document records local evidence
only and must not be read as a remote-CI success claim.
