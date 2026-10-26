# RP-28C validation (Linux x86_64)

Date 2026-10-26, on `main` after 9350a51. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass; a fresh build takes 96 s on 32 cores (was 195 s before the split) |
| `uv run pytest -q -m 'not dist and not inventory'` | 203 passed, 31 deselected (568 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/app tests/coverage` | 52 passed, including the manifest checked against the built package's introspection |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

In actual Rack's live UI (`tests/app/in_rack/test_components.py`):

- Every concrete component is constructed by Rack's own constructor, with
  Rack's installed SVGs. The knobs have SVGs, angles and `bg`/`fg` host
  children; switches have their frames, ports and screws have their sizes,
  and the latch and momentary flags are Rack's.
- The aliases and colours match Rack's, and a Python subclass runs Rack's
  constructor.
- Light templates: colour bases, Rack's light SVGs, the mm sizes of the
  simple lights, bezel and PB61303 lights. Specializations are cached
  classes deriving from their template, and the alias templates give the
  aliased classes. Bad template arguments, and instantiating a template
  class, raise `TypeError`.
- `RectangleLight[GreenLight]`, rendered by Rack: the background on layer 0,
  and Rack's green on layer 1 (brightened by Rack's own halo).
- Light sliders and buttons: the light follows the slider's handle and sits
  at the button's centre. `create_light_param_centered` wires the light to
  an engine module, and a control without `get_light` is refused.
- `SegmentDisplay.set_lights` makes `RectangleLight[RedLight]` segments
  with consecutive light IDs, on Rack's black background.
- A component port reaches its module's output.

Not covered here: the audio and MIDI buttons (RP-28D, with the displays).
Components cabling in a Python module widget are covered by
`tests/app/test_module_browser.py`'s factory ports, which use `SvgPort`.
Linux x86_64 only.
