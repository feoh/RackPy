# RP-24D validation (Linux x86_64)

Date 2026-10-26, on `main` after e889ab2. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run pytest -q tests/engine/ops tests/engine/test_host.py` | 3 passed |
| `uv run pytest -q -m 'not dist and not inventory'` | 208 passed, 31 deselected (612 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/engine/ops/in_rack/test_foreign_modules.py`),
with Rack Free's Core models:

- **Core's Notes.**
  - `create_module()` gives a Python-owned `EngineModule` with no ID, which
    the engine does not have.
  - Making its widget is refused until `add_module`, and `add_module` gives
    it an ID.
  - Adding it again, and a widget from the wrong model (Blank), are refused.
  - `create_module_widget(module)` gives a `ForeignModuleWidget`, 3U high. A
    second widget is refused, and so is removing the module while the widget
    owns it.
  - `add_module_at_mouse` hands it to Rack's rack, and the handle reports it
    added.
  - Rack's own `ModuleRemove`, filled by `set_module` from the rack's view,
    deletes and recreates the module. The `EngineModule` handle goes stale
    and comes back with the module's ID.
- **Previews and dropped handles.**
  - A Blank preview is refused by the rack, and `close()` is idempotent.
  - A dropped MIDI-Map module (whose constructor adds param handles to
    Rack's engine) is deleted safely, and the module count is unchanged.
  - Closing a widget deletes its module.
  - A widgetless Blank module in the engine goes back to Python with
    `remove_module`.
- **The RuntimeProbe** (a C++ model in RackPy's test plugin) gives a Rack
  widget whose preview the rack refuses.
- The live suite's `remove_module` of the probe now raises "widget owns it".

Windowed Rack (`tests/engine/test_host.py`), the `rackpy.engine.host` facade
with a Python model:

- **Context.** `engine()` raises `ContextError` at plugin start-up, and
  `modules()`/`module()` find the probe.
- **Cables.** `connect` joins two Python modules (and the input reads as
  connected), and refuses another plugin's module. `disconnect` removes the
  cable.
- **Param handles.** `map_param` maps with text and colour, and a failed
  mapping (out-of-range param) removes its handle again. `unmap_param`
  removes the handle.
- **Undo steps.** `undo_step` pushes one step for two param changes, and
  Rack's undo restores both. A failing block is undone, and nothing is
  pushed: the redo of the earlier step survives.

Only Linux x86_64 was tested.
