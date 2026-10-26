# RP-28A validation (Linux x86_64)

Date 2026-10-26, on `main` after e707cf3. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | 202 passed, 31 deselected (478 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/app` | 2 passed: 14 in-Rack tests in the live UI, and a windowed Rack session |
| `prek run --all-files`, `uv run bin/generate_bindings.py check` | pass |
| `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | 0 warnings |

In actual Rack, through Rack's EventState:

- a knob dragged up raises its parameter's (smoothed) value, and a switch
  click steps to its next value;
- `reset_action` restores the default;
- `touchedParam` points at the clicked knob and is released when the knob is
  deleted and when its module is;
- deleting the module clears every control's `module`, and each control
  then takes Rack's browser-preview path;
- the module's lights drive a ModuleLightWidget's colour (Rack's square-root
  brightness);
- a Python LightWidget's background, light and halo overrides run on Rack's
  layers 0 and 1 (pixels read back);
- a Python port's drag override runs, and Rack makes no cable;
- Rack calls `append_context_menu` with its own menu, which the override
  fills;
- closing the knob closes that menu;
- SVG controls size to their SVGs, and their host children (framebuffer,
  SVG, transform, shadow) are reachable and stale after the control dies;
- unloaded SVGs and out-of-range IDs are refused, as are detached and
  under-construction modules;
- the factories set Rack's fields in Rack's order;
- the deprecated aliases warn.

Windowed Rack (`tests/app/test_module_widget_controls.py`) runs a Python
model, `PyJacks`, from Rack's module browser path, with Python controls
added to its ModuleWidget:

- a click sets `touchedParam`;
- ports make no cable;
- Rack's Delete action deletes the widget with its Python controls and
  releases `touchedParam`;
- undo restores the module;
- a second module, kept until Rack quits, is deleted cleanly, and Rack exits
  with status 0.

That test disables Rack's first-launch tip window (`showTipsOnLaunch`), which
otherwise covers the rack and takes the clicks.

Not covered here: cables between Python ports, which need RP-28B. Linux
x86_64 only.
