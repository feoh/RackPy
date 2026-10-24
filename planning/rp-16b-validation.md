# RP-16B validation (Linux x86_64)

RP-16B adds assets, the plugin's C hooks, entry diagnostics and the Module
class facade. It does **not** complete RP-16; see [the slices](rp-16-slices.md).

Date 2026-10-24, on `main` after 71f3bd6..eeeab06 (RP-15C, RP-25, RP-16A).
Pinned inputs: GCC 16.2.1, uv 0.9.18, Rack SDK and Rack Free 2.6.6
(061ccf63), CPython 3.14.9, pybind11 3.1.0. Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 4/4 CTests pass |
| `uv run pytest -q tests/plugin` | 8 passed, before the last two in-Rack tests were added (the full run covers them) |
| `uv run pytest -q -m 'not dist and not inventory'` | 187 passed, 31 deselected (290 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `prek run --all-files` (ruff, format, strict mypy, planning, callback matrix, manifest) | pass |
| `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass |
| `bin/planning-check` | pass |

What the actual-Rack tests establish:

- Asset paths join onto Rack's folders exactly as Rack's functions do. The
  directory variables are Rack's values, and `bundlePath` is empty on
  Linux. The pure functions work off the main thread.
- `plugins` lists Core first and this plugin once it has loaded. It slices,
  indexes and refuses out-of-range indices. `pluginsPath` contains this
  plugin. The library handle is an identity-only object (no `__int__` or
  `__index__`), and Core has none.
- `Model.from_json` applies a manifest entry, drops unknown tags as Rack
  does, refuses wrong types before Rack sees them, surfaces Rack's
  exception for an empty name, and refuses another plugin's and detached
  models.
- A `ModuleClass` module keeps its instance for exactly the module's
  lifetime, and its state round-trips. Registered through `add_model`, it is
  saved by Rack's exit autosave and reloaded at the next launch.
- `destroy()` runs `on_destroy` callbacks newest first, contains one that
  raises, and Python still finalizes (`Py_FinalizeEx returned 0`).
- A missing entry module and a missing dependency are named in Rack's log,
  with the traceback.
- A dropped, never-added model releases its factory.

No Rack or Xvfb processes were left behind.

## Gates not established

macOS (where `bundlePath` is set), Windows, the distributable build and
sanitizers are not covered, nor anything still listed on the RP-16 parent.
