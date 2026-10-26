# RP-24B validation (Linux x86_64)

Date 2026-10-26, on `main` after 18f252a. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | 203 passed; the 2 coverage tests failed on a renamed test ID, fixed, then `uv run pytest -q tests/coverage` 49 passed |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/engine/ops` | 2 passed: the live-UI suites and the windowed session |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |
| `bin/planning-check` | 45 tasks, acyclic |

In actual Rack's live UI (`tests/engine/ops/in_rack/test_cables_handles.py`):

- A Python `Cable`:
  - the defaults are as expected;
  - `to_json` needs both modules;
  - the ID range and module types are checked;
  - patch JSON round trips with the RuntimeProbe as both ends, and
    `from_json` resolves EngineModules;
  - an unknown module raises `ValueError`.
- The engine's cables:
  - lookups work;
  - `add_cable` refuses another plugin's module, and refuses a cable whose
    other end is a module the engine does not have;
  - `remove_cable` refuses a cable the engine does not have.
- A Python `ParamHandle`:
  - adding it, and the refusal to add it again;
  - mapping it to the probe's param, and `module`;
  - Rack's deprecated lookup by module, with a warning;
  - range checks, and the hidden sentinel;
  - Rack's overwrite rules, both ways;
  - Rack's unmapping of a handle re-mapped to its own param;
  - removing it, and the refusals after removal and for a mapped handle.
- The four `_NoLock` methods raise `EngineLockError` outside a locked
  callback.

Windowed Rack (`tests/engine/ops/test_engine_ops.py`). The patch has the
probe and two PyTone modules joined by a cable with Rack's cable widget:

- **A Python cable between two RackPy modules Python put into the engine.**
  - It is in the engine, found by ID, and saved in `to_json`.
  - The input reads as connected.
  - Removing a cabled module is refused, and so is giving it a widget.
  - A cable to a module with a widget is refused.
- **The patch's widget cable.** It is found by ID, its endpoints resolve,
  and `remove_cable` refuses it.
- **Removal and re-adding.** Removed, the cable is Python's again: it keeps
  its ID and the input reads as disconnected. Re-added, it gets the same ID.
- **A Python param handle** maps a browser-added module's param.
- **Inside a ResetEvent (Rack's exclusive lock):**
  - `get_module_no_lock` and `get_param_handle_no_lock` work;
  - `update_param_handle_no_lock` unmaps and remaps;
  - `set_master_module_no_lock` sets the master and restores it.
- **Clearing.** From the scene-level step (`call_after_ui_step`), Rack's rack
  is cleared, then Rack's engine.
  - Python's handle, cable and widgetless module survive the rack's clear.
  - After the engine's clear, the handle reads as not in the engine, the
    cable and module are stale, and the engine is empty.
  - `remove_param_handle` is refused rather than failing Rack's assertion.
  - The handle can be blanked and added again.
- **Shutdown.** A handle left in the engine is removed at context close, and
  Rack exits cleanly.

Not covered: cables with widgets and audio/MIDI displays (RP-28E), history
(RP-24C) and other plugins' models (RP-24D). Only Linux x86_64 was tested.
