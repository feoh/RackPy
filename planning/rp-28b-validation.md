# RP-28B validation (Linux x86_64)

Date 2026-10-26, on `main` after 4cd9408. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | 203 passed, 31 deselected (497 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/app` | 3 passed: the in-Rack suites (live UI) and two windowed Rack sessions |
| `uv run pytest -q tests/runtime tests/widget` | pass, with the shutdown order now starting at scene deletion |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**The actual-host gate** (`tests/app/test_module_browser.py`): windowed Rack,
Rack Free 2.6.6, a Python model whose widget is a Python `ModuleWidget`,
with original SVG artwork. Rack's first-launch tip window is turned off with
`showTipsOnLaunch`, since it would cover the rack.

1. Rack's real module browser, searched by typing, builds the model's
   preview through the Python factory, with no module.
2. A click on that preview runs the browser's `chooseModel`, which adds the
   module and its Python widget.
3. A cable dragged between two such modules' factory-made ports appears in
   Rack (one CableWidget) and in the engine. The input is connected.
4. Rack's Delete: the widget's Python child gets RemoveEvent once and is
   then stale, and the cable goes.
5. Undo rebuilds the widget through the Python factory, and Rack restores
   the cable on the new ports. Redo removes both again, and a second undo
   restores them.
6. ModuleWidget's own calls on the widget in the rack all ran: `to_json`,
   `from_json`, `save`, `load`, `save_template`, `has_template`,
   `load_template`, `clear_template`, `copy_clipboard`,
   `paste_clipboard_action`, `paste_json_action`, `reset_action`,
   `randomize_action`, `bypass_action`, `clone_action` (the copy is built by
   the factory), `disconnect_action` and undo, and `create_context_menu`.
7. `Model.create_module()` and `create_module_widget()` from Python: Python
   owns both, a second widget for the same module is refused, and so is
   `set_module` outside a factory. Closing the owning widget deletes its
   module.
8. The documentation's `module_widget.py` model is added through Rack and
   built by its factory.
9. Rack quits and saves its autosave. A second launch reopens it: every
   module's widget is rebuilt by the factory, and the cables are restored on
   the rebuilt ports.

Both launches exit with status 0.

This gate found that RackPy's shutdown at window close deleted the Python
module widgets, and their modules, before Rack wrote the autosave. The
shutdown now begins at scene deletion (ADR 003 decision 13, amended), and
`tests/runtime` checks that order.

Not covered: other plugins' models through `Model.create_module` and
`create_module_widget` (RP-24); `appendDisconnectActions` (history family);
the dialog methods, which open file dialogs and wait for the user, so no
test calls them. Linux x86_64 only.
