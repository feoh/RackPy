# RP-28D validation (Linux x86_64)

Date 2026-10-26, on `main` after d5a25ca. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | 203 passed, 31 deselected (574 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/app` | 3 passed: the in-Rack suites (live UI) and the windowed sessions |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/app/in_rack/test_rack.py`):

- The scene's parts are typed views, the mouse position is readable, and
  Rack's deprecated `mouse_pos` field is writable.
- The rack scroll widget's zoom is clamped by Rack, its grid offset round
  trips, and the bound and zoom checks hold.
- The RuntimeProbe's module widget (Rack's) can be selected and reported in
  `selection_to_json`, moved with `request_module_pos` and
  `set_module_pos_force`, and found with `get_module`.
- `merge_json` adds positions, and `from_json` refuses a module that
  already has its widget.
- The containers, cable queries, the next cable colour and the selection
  context menu work.
- `set_touched_param` refuses a param without a module, takes one with a
  module, and releases it when that param dies.
- Host dispatch: Rack's `onDirty` on the menu bar runs, and Rack's drawing
  of the menu bar draws into a Python framebuffer. Add, Remove and the
  context events are absent.
- The LED display widgets have Rack's fields and draw, text field included.

Windowed Rack (`tests/app/test_module_browser.py`, the RP-28B gate,
extended):

- The cable dragged between two Python modules is a `HostCableWidget`. Its
  ports are the Python ports, it is complete, and its plugs are
  `HostPlugWidget`s pointing back at it.
- `get_top_cable`, `get_cable(out, in)`, `get_cables_on_port`,
  `get_complete_cables_on_port` and `get_top_plug` find it.
- The positions are finite, `merge_json` adds the colour, and `color` and
  `from_json` set it.
- A Python module widget goes into the rack with `add_module_at_mouse` and
  comes out with `remove_module`, owned by Python again with its module
  still in the engine.

Found: Rack 2.6.6's `RackWidget::hasModules` is inverted (it returns
`children.empty()`). The binding keeps Rack's value and documents it.

Not covered here: RP-28E's records and the dialog methods (which open file
dialogs and wait for the user). Linux x86_64 only.
