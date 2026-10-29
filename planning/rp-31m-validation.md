# RP-31M — Plugin destructor and model lifetimes

## Contract and implementation

Rack 2.6.6 `Plugin::~Plugin()` clears each attached model's `plugin`
back-pointer; it does not delete those models. Loaded Rack plugins remain
Rack-owned and Python cannot close them. `rackpy.raw.plugin.Plugin()` now
creates an isolated plugin, not registered with Rack, which can attach only
unattached RackPy models. Its explicit, idempotent main-thread `close()` runs
the Rack destructor before releasing its retained model ownership. Surviving
Python model handles stay live with `model.plugin is None` and can be
reattached to another isolated plugin. A main-thread context-shutdown hook
closes any isolated plugins left open. Python GC alone never closes one: the
registry deliberately retains plugins and their models until close or shutdown.

An isolated, detached, or staged model cannot create a module or widget:
those retain a raw `Model*`, which could otherwise outlive that model. Rack's
committed models and foreign loaded models retain their existing creation
paths. Attachment reserves the retaining vector before Rack's `addModel`, and
restores the back-pointer if list allocation fails. Pointer identities for
isolated plugins carry a generation to avoid reusing an old wrapper after
address reuse.

This isolated-plugin exception to the previous this-plugin-only `add_model`
policy was selected after the user authorized proceeding with recommended
decisions. The corresponding destructor manifest entry is implemented, not
verified; no records were promoted to verified.

## Linux validation

- Linux x86_64, Rack Free 2.6.6: Werror test-hooks build and all 6 CTests
  passed. Actual-Rack `tests/plugin/test_plugin.py`, built-package
  introspection and documentation-snippet tests: 17 outer tests passed. The
  in-Rack plugin suite covers real destructor back-pointer clearing, surviving
  model and factory lifetimes, repeated close, reattachment, dropped handles,
  model reacquisition, rejection of duplicate attachment and module/widget
  escapes, main-thread restrictions, and refusal to close Rack-owned plugins.
  It deliberately leaves one isolated plugin open; the host exits cleanly
  through context shutdown (this is not an independent destructor counter).
- `tests/unit`: 26 passed. Sphinx `-W`, `prek run --all-files` (including
  strict mypy), generated-file checks and source-only coverage checks passed.
  The host introspection test checked Python names using the hooks artifact.
- The host runs were serialized with `/tmp/rackpy-host-tests.lock` and used a
  temporary `tests/conftest.py` override pointing the build fixtures to the
  existing `build/dev` test-hooks artifact; the override was restored after
  each run. This is not a fresh production-build validation. macOS and
  Windows have not been tested in this continuation.
