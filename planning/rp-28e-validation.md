# RP-28E validation (Linux x86_64)

Date 2026-10-26, on `main` after 77e2173. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/app` | 4 passed: the live-UI suites (with `test_io_widgets.py`) and the windowed sessions (`test_cables.py`) |
| `uv run pytest -q -m 'not dist and not inventory'` | 213 passed, 31 deselected (640 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/app/in_rack/test_io_widgets.py`):

- **Choices.** An `AudioDriverChoice` starts with no port and its `step()`
  shows Rack's no-driver text. With a Python port it shows the port's
  driver. The other audio choices step with a port. The MIDI choices show
  Loopback and Loopback 3 for a loopback port. An audio choice refuses a
  MIDI port.
- **Displays.** `AudioDisplay.set_audio_port` with the only reference to a
  port builds Rack's choices and separators and steps after a GC, so the
  pinned port stays alive. `MidiDisplay` does the same.
- **Buttons and menus.** The buttons keep their ports. The component
  library's `AudioButton_ADAT`, `AudioButton_USB_B` and
  `MidiButton_MIDI_DIN` load Rack's artwork. `append_audio_menu` and
  `append_midi_menu` fill a Python menu.
- **Containers.** Rack's cable, module and plug containers refuse other
  widgets. A cable widget refuses other parents, a Python call to
  `on_add`, and `update_cable` outside the rack.

Windowed Rack (`tests/app/test_cables.py`), with PyTone modules 2, 3 and 4
and their factory ports:

- **Rack's drag flow.**
  - A cable widget's plugs are `HostPlugWidget`s, and a wrong-direction
    port is refused.
  - `update_cable` is refused while detached. In the rack, the widget is
    complete without an engine cable, then `update_cable` makes the engine
    cable and connects module 3's input.
  - `merge_json` gives the colour, and the rack lists the widget.
- **Rack's history.** `CableAdd.set_cable` and Rack's undo delete the
  Python widget and its cable; Rack's redo makes Rack's own widget, which
  `remove_cable` refuses.
- **A Python cable adopted by a widget in the rack.** `set_cable` takes its
  ports. `release_cable` takes it out of the engine too, and Python owns
  it. After re-adoption, `remove_cable` gives the widget back to Python,
  alive and parentless, without an engine cable.
- **Weak ports.** A detached widget's input port is dropped once that
  module widget is deleted.
- **The documentation's snippet** (`docs/snippets/cable_widget.py`) connects
  modules 2 and 4 with an undo step, and Rack's undo removes it.
- **The reclaim fix.** Plain and subclassed cable widgets come back from
  `remove_cable` and `remove_from_parent` as the same, live Python objects
  (checked with a live-UI probe during development).

Only Linux x86_64 was tested.
