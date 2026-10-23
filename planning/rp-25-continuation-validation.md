# RP-25 continuation validation (Linux x86_64)

This checkpoint continues the existing RP-25 parent after RP-25A–D. It does
**not** complete RP-25, any full Widget mapping, or the implementation phase.

## Change boundary

- `058bede`: checked raw `WidgetNode.box` assignment and live nested box/vector
  field views. Every access rechecks native lifetime, context and UI thread.
  Views survive attached-wrapper collection but do not retain native ownership.
  `get_box()` stays a copy; explicit view `copy()` gives ordinary Rect/Vec values.
- Follow-up: direct `requested_delete` bool, `request_delete()` and native-base
  `step()`. Pending/cancelled requests preserve Rack semantics, hidden branches
  still step, deleted descendants invalidate retained handles/geometry views,
  and remove/re-add preserves a pending request. Real Context shutdown also
  invalidates retained geometry views.

All four exact manifest entries stay **planned**. In particular, views do not
implement every Rect/Vec operation or implicit conversion, and native `step()`
is not a Python override mapping. No callback, scene, browser, drawing, or
window-recreation coverage is implied.

## Passing local checks

Run from `/tmp/rackpy-rp25-continuation`, against the checksum-verified pinned
Rack SDK/Free host and pinned CPython/pybind11 inputs:

- `bin/rackpy-dev build --werror --build-dir build/dev --test-hooks`: compiled
  native plugin and all **4 CTests** passed, including widget aliasing,
  replacement, collection, deletion and invalidation checks.
- `uv run pytest -q tests/widget/test_tree.py tests/coverage/test_introspection.py`:
  **5 outer host tests passed**. The widget suite runs inside actual Rack's
  live UI under Xvfb, executes the docs snippet, checks stale/foreign/worker
  access, and checks retained native roots/views at real host shutdown.
- `uv run pytest -q -m 'not dist and not inventory and not gui and not host'`:
  **93 passed, 107 deselected** when run without a concurrent Sphinx build.
  This includes the **61 unit tests**, native and docs regression tests.
- `prek run --all-files`, then `uv run mypy`: all hooks and strict typing pass.
- `bin/planning-check`, `bin/generate_bindings.py check`, and `git diff --check`:
  pass without reducing or promoting the coverage denominator.
- `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html`:
  passes with warnings as errors.

## Incomplete/failed attempts and cleanup

The all-host `uv run pytest -q -m 'not dist and not inventory'` attempt hit its
180-second tool timeout during the deliberate unsafe-worker deadlock probe.
It is **not** a full-suite pass. The orphaned Rack, xvfb-run and Xvfb processes
from that attempt were terminated and checked absent. No harness change is
included here.

An initial broader regression run collided with a simultaneous Sphinx command:
AutoAPI's shared generated source directory was removed while the other build
was using it. That attempt had 91 passes and 2 docs failures. Re-running the
same regression command serially passed all 93 tests. Do not run two Sphinx
builds against this worktree concurrently.

Temporary docs logs were outside git and removed after recording results.
The existing continuation worktree is retained as the local implementation
checkpoint, rather than removed with its unpushed commits.

## Gates not established

No full distribution build, sanitizers, macOS, Windows, full all-host suite,
performance measurement, Python Widget subclass/callback exception handling,
scoped event propagation, module-browser/null-preview/scene insertion or graphics
window recreation was established. The parent contract remains open; the next
major work is full Widget subclass/scoped-event ownership and containment, plus
remaining raw field/container and variant semantics.

## Checkpoint: subclassable widgets, typed bases and scoped events

Date: 2026-10-23. The continuation branch was rebased onto local `main` at
`74d2c9e` (RP-15B) first; only `planning/witan.json` and generated files
conflicted, and every rebased commit passes `bin/generate_bindings.py check`
and `bin/planning-check` (`git rebase --exec`). GCC 16.2.1, uv 0.9.18, the
checksum-verified Rack SDK and Rack Free 2.6.6, bundled CPython 3.14.9 and
pybind11 3.1.0. Host runs serialized with `flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/dev` | compiled; 4/4 CTests passed |
| `uv run pytest -m "not dist and not inventory" -q -p no:randomly` | 179 passed, 31 deselected, 266 s, all host and GUI tests included |
| `uv run pytest -m inventory -q` | 29 passed |
| `uv run pytest -m unit -q` | 70 passed |
| `prek run --all-files`, `bin/rackpy-dev typecheck` | all hooks and strict mypy pass |
| `uv run sphinx-build -W --keep-going -n -b html docs <tmp>` | passes; one reviewed `nitpick_ignore` for the stub-only `_BaseEvent` alias |
| `bin/generate_bindings.py check`, `bin/planning-check`, `git diff --check` | pass |

The live-UI suite now includes `test_drawing.py` (Python `draw`, `draw_vg`,
`draw_layer`, `draw_child` and `draw_framebuffer` driven by Rack's
`FramebufferWidget.render()`, expiring draw records, refused mutation and
nested render, contained draw errors, and the docs snippet) and
`test_scene.py` (scene attachment and detachment, and a scene widget that
receives Rack's real `ContextDestroy` from the Window destructor before
RackPy's shutdown sweep deletes it, checked by the `scene-widget-teardown-ok`
marker). Their outcomes were confirmed individually, not only through the
outer test's all-passed assertion.

The earlier "all-host timeout" was the 180-second tool limit, not a hang: the
same suite completes in 266 s. No Rack or Xvfb process was left running.

229 manifest records are **planned**, none implemented or verified.

Not run or not established: the `dist` tests, sanitizers, macOS, Windows,
remote CI, frame-loop delivery to scene widgets (tests run inside one UI
step), real mouse/keyboard input, module browser previews, module removal,
and window recreation. No performance claim.

## Checkpoint: module removal, browser previews and graphics recreation

Same environment and date. Four test-only RuntimeProbe hooks follow Rack's
code (`add_probe_module`, `remove_probe_module`, `render_browser_preview`,
`recreate_graphics_context`); `tests/widget/in_rack/test_scene.py` uses them.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/dev` | compiled; 4/4 CTests passed |
| `uv run pytest -m "not dist and not inventory" -q -p no:randomly` | 179 passed, 31 deselected, 264 s |
| `uv run pytest -m inventory -q` | 29 passed |
| `prek run --all-files`, `bin/rackpy-dev typecheck` | pass |
| `uv run sphinx-build -W --keep-going -n -b html docs <tmp>` | passes |
| `bin/generate_bindings.py check`, `bin/planning-check`, `git diff --check` | pass |

The three new in-Rack tests' outcomes were confirmed individually. A first
run failed honestly and was fixed: the start-up run could not import the
hooks module (the production bundle has none), and the window-teardown
marker was missing because the simulated recreation also reaches the
scene-attached teardown probe; its check now requires the real teardown to be
the last, unpaired destroy.

These hooks exist only in test-hooks builds. They are not the real module
browser window, a user deleting a module, or a host that recreates its
window. An actual-host smoke with Python module widgets waits for RP-16 and
RP-28.
