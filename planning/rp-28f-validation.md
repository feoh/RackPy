# RP-28F validation (Linux x86_64)

Date 2026-10-26, on branch `rp28f-host-widgets`, rebased on `origin/main`
4ef82b2. Pinned inputs as in [rp-27a-validation.md](rp-27a-validation.md):
Rack 2.6.6 (`061ccf63c1758599396ac1bb10d47345d9d34076`) and its SDK. Host
runs were serialized with `flock /tmp/rackpy-host-tests.lock`. Logs are in
`build/validation/rp28f-*.log`, which git ignores.

The decision and the records are in [rp-28-slices.md](rp-28-slices.md) (RP-28F).

## Header and source evidence

These were read from the pinned SDK
(`~/.cache/rackpy/sdk/x/lin-x64/Rack-SDK/include`) and Rack's source
(`~/.cache/rackpy/src/Rack` at the pinned commit). `cmp` showed that the four
headers are the same in both.

| Lines | Text |
| --- | --- |
| `include/app/RackScrollWidget.hpp:19-20` | `PRIVATE RackScrollWidget();`, `PRIVATE ~RackScrollWidget();` |
| `include/app/RackWidget.hpp:28-29` | `PRIVATE RackWidget();`, `PRIVATE ~RackWidget();` |
| `include/app/Scene.hpp:27-28` | `PRIVATE Scene();`, `PRIVATE ~Scene();` |
| `include/app/CableWidget.hpp:20-21` | `PlugWidget();`, `~PlugWidget();` (public) |
| `src/app/CableWidget.cpp:36-49` | `PlugWidget::Internal`: `cableWidget` and `type` have no initializers |
| `src/app/CableWidget.cpp:52-103` | `PlugWidget()` does `new Internal` and never sets them; `step()` calls `internal->cableWidget->getPort(...)` |
| `src/app/CableWidget.cpp:140-151` | `CableWidget()` makes both plugs and sets their `cableWidget` and `type` |

The supervisor's first estimate of `Scene.hpp` was 28-29; the actual lines
are 27-28. Two source ranges were also corrected, from 36-50 and 140-150 to
36-49 and 140-151.

## Commands

| Command | Result |
| --- | --- |
| `uv sync --frozen` | ok |
| `bin/rackpy-dev build --werror --test-hooks` | builds; CTest 100% of 6 pass |
| `uv run pytest -m unit` | 100 passed, 201 deselected |
| `prek run --all-files` | all hooks pass (ruff, ruff format, strict mypy, planning data, callback matrix, binding manifest and generated files, JSON/YAML/TOML, whitespace) |
| `bin/rackpy-dev typecheck` | strict mypy: no issues, exit 0 |
| `bin/planning-check` | 45 tasks, 114 dependencies, acyclic; TASKS.md and witan.json agree |
| `uv run bin/generate_bindings.py check` | generated files match `bindings/manifest.json` |
| `uv run bin/check_coverage.py check --without-built-package`, before | absent 2, excluded_internal 111, implemented 2890, planned 54, unreviewed 1 |
| the same, after | absent 2, excluded_internal 111, implemented 2911, planned 33, unreviewed 1 |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` (serial, fresh output directory) | build succeeded, 0 warnings |
| `uv run pytest -m "docs and not host"` | 15 passed, 286 deselected |
| `flock /tmp/rackpy-host-tests.lock uv run pytest -q -rA tests/app tests/coverage/test_introspection.py tests/docs/test_snippets.py` | 12 passed in 446 s |

The host run was on the committed tree, with a clean worktree. It covered:

- `tests/app/test_app.py`: every live-UI suite under `tests/app/in_rack/`,
  including the new test;
- `test_cables.py`, `test_module_browser.py` and
  `test_module_widget_controls.py`, the windowed sessions that use
  `HostRackWidget` and `HostPlugWidget`;
- `test_introspection.py`, including the manifest checked against the
  built package;
- `test_snippets.py`, including the snippets that run in Rack.

The suite runs inside Rack, and `test_app_widgets_in_live_rack` passes only
if every in-Rack test passes. It does not print its own count. An earlier
development run of the same suite reported `1 failed, 36 passed`: a first
version of the held-arrow check scrolled right, past Rack's clamp. So the
suite has 37 tests.

## In-Rack evidence

`tests/app/in_rack/test_rack.py::test_racks_own_scene_rack_and_plugs_are_not_subclassable`
ran in Rack Free 2.6.6's live UI under Xvfb.

- **Refusals.** `rackpy.raw.app` has no `Scene`, `RackScrollWidget`,
  `RackWidget` or `PlugWidget`. `HostScene()`, `HostRackScrollWidget()`,
  `HostRackWidget()` and `HostPlugWidget()` raise pybind11's "No
  constructor defined". A Python subclass of any of them raises "not an
  acceptable base type", because they are now `py::is_final()` (and
  `@final` in `rackpy/raw/app.pyi`).
- **Scene, Rack's instance.**
  - `step()` steps a Python child attached to the scene.
  - `on_path_drop` with a `.txt` path reaches the child under it, which
    becomes the target.
  - `on_hover` and `on_drag_hover` set `get_mouse_pos()`.
  - `on_hover_key` with Ctrl+0 resets the zoom to 1 and consumes the event.
  - A held Left arrow, then `step()`, scrolls the rack left by more than one
    grid unit.
- **RackScrollWidget.**
  - Page Down pages the offset.
  - A hover scroll scrolls or zooms, as Rack's settings choose.
  - The fourth mouse button zooms to 2^-0.5.
  - `on_hover` passes the hover to the rack, whose mouse position changes.
  - `step()` clamps an offset of 1e5 grid units.
- **RackWidget.**
  - `on_hover`, `on_drag_start`, `on_drag_hover` and `on_drag_end` draw
    Rack's selection rectangle. It selects the RuntimeProbe's module widget,
    and once the drag ends the selection stays.
  - A left click is consumed with the rack as its target.
  - `on_hover_key` stops propagation (OpaqueWidget's).
  - `step()` runs.
- **Drawing.** The scene, scroll and rack views draw into a framebuffer
  inside a Python drawing callback.
- **PlugWidget.**
  - A Python `CableWidget`'s `input_plug` is a `HostPlugWidget` whose
    `get_cable()` is that cable widget.
  - `step()` runs Rack's `PlugWidget::step` on that plug, which Rack's
    constructor made.
  - Once the cable widget is closed, the view raises `StaleHandleError`.

`tests/coverage/test_callback_paths.py::test_host_only_app_widgets_map_to_calls_on_racks_own_instances`
(unit) checks the 21 records and their generated callback-path rows:

- each record is `implemented`, with the decision prefix and its header or
  source lines;
- each keeps the RP-28D text and names the new in-Rack test;
- each row says "Call on Rack's own instance".

## Not run

- macOS and Windows: no builds exist (RP-40, RP-41).
- The whole `host` suite (`pytest -m "host and not dist"`), the `dist` and
  `inventory` suites, and `pytest -m "native and not host"`. Host runs were
  kept to the suites this change touches, because the lock is shared.
- Real input devices: Python made the events. Rack's own event dispatch
  (`EventState`) was not driven by a real mouse or keyboard.
- The owner's review of the decision is still pending.

## After integration

Rebased by the supervisor onto `465b22d` (getDeviceDetail recorded as
declared but never defined), with generated files regenerated (implemented
2912, planned 33, excluded_internal 111, absent 2). Supervisor review of the
diff: exactly the 21 records changed, planned to implemented, each with the
decision prefix and its RP-28D text intact. On the combined state:
`bin/rackpy-dev build --werror --test-hooks` (CTest 6/6), `uv run pytest -m unit`
(101 passed), `tests/native/test_sdk_symbols.py` and
`tests/coverage/test_callback_paths.py` (5 passed), `prek run --all-files`,
`bin/rackpy-dev typecheck`, the sphinx `-W` build,
`uv run pytest -m "docs and not host"` (15 passed) and
`uv run pytest -m "host and not dist" tests/app tests/coverage/test_introspection.py tests/docs/test_snippets.py`
(9 passed, 453 s).
