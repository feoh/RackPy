# RP-24C validation (Linux x86_64)

Date 2026-10-26, on `main` after 6796ec3. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | first run: 203 passed, 4 failed (callback strategies the matrix requires, and the family allow-list); fixed, then 207 passed, 31 deselected (608 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/history` | 2 passed: the live-UI suite and the windowed session |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/history/in_rack/test_history.py`), with the
RuntimeProbe as the target module:

- **Python-made actions.**
  - Fields, defaults and names; module JSON as Python data.
  - `ParamChange` sets the probe's value both ways, and does nothing with
    an out-of-range param ID.
  - `ModuleBypass` bypasses and restores.
  - `ModuleChange` loads its JSON and does nothing without one.
  - `ModuleAdd` without a model, and `CableAdd` with an out-of-range output,
    do nothing.
  - A Python `ModuleAction` subclass runs its own undo and redo.
- **ComplexAction.** It owns its children (the same Python objects), refuses
  re-pushing and containing itself, and undoes backwards.
- **InverseAction.**
  - It is a template: not instantiable itself, and refuses non-actions.
  - Specializations are cached subclasses.
  - `InverseAction[ParamChange]` runs Rack's checked code swapped, and a
    Python action's methods are swapped too.
- **Rack's history.**
  - `push` transfers the action and refuses it a second time.
  - Names, the saved flag and index, and undo/redo of a Python action.
  - A push from inside an action's undo is refused ("would delete the
    running action").
  - A Python action that raises during `State.undo` is reported, and the
    index moves.
  - A push after an undo deletes the undone Python action, whose object
    then raises.
- **The documentation's snippet** (`docs/snippets/history_action.py`)
  changes a param with an undo step, and undo and redo restore it.

Windowed Rack (`tests/history/test_history.py`). The patch has the probe and
two PyTone modules with Python module widgets, joined by cable 7:

- **Rack's own actions.** Rack's `bypass_action` step is an ActionView
  (`rack::history::ModuleBypass`, module 2). Undo and redo apply, and the
  view stays alive. Rack's `disconnect_action` step is a ComplexAction view
  whose child is a `CableRemove`, and undo restores the cable.
- **Disconnect actions.** `append_disconnect_actions` fills a Python
  ComplexAction and removes the cable, as Rack does. Pushed and undone, it
  restores the cable.
- **Python cable actions.** `CableAdd.set_cable` and `is_cable` read the
  real cable widget. A Python `CableRemove`'s redo deletes cable 7, and its
  undo recreates it with ID 7. A `CableAdd` with a missing input does
  nothing.
- **Module drag.** `get_module_drag_action` after
  `update_module_old_positions` and a forced move gives a ModuleMove view.
  Pushed, it undoes and redoes the move.
- **Python module actions.** A Python `ModuleMove` moves the widget. A
  Python `ModuleRemove`, filled by `set_module`, deletes module 3 and its
  widget on redo, and on undo recreates both through the Python model's
  factories.

Only Linux x86_64 was tested. Not covered: Rack's dialogs and menus driven by
input events.
