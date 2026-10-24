# RP-16A validation (Linux x86_64)

RP-16A registers Python models. It does **not** complete RP-16; see
[the slices](rp-16-slices.md).

Date 2026-10-24, worktree `/tmp/rackpy-rp16a`, branch
`rp-16a-model-registration`. The branch is stacked on `rp-25-children` (PR
https://github.com/feoh/RackPy/pull/4) and merges `rp-15c-json-hardening` (PR
https://github.com/feoh/RackPy/pull/3). Pinned inputs: GCC 16.2.1, uv 0.9.18,
Rack SDK and Rack Free 2.6.6 (061ccf63), CPython 3.14.9, pybind11 3.1.0. Host
runs serialized with `flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 4/4 CTests pass |
| `uv run pytest -q tests/plugin` | 5 passed: the in-Rack API suite (7 tests, live UI), the two-launch Rack round trip, the manifest mismatch, the registration errors and the failing entry module |
| `uv run pytest -q -m 'not dist and not inventory'` | 184 passed, 31 deselected (286 s), after the fix below; includes the `register_models.py` snippet run inside Rack (its models matched `plugin.json`) and the manifest check against the built package |
| `uv run pytest -q -m inventory` | 29 passed |
| `prek run --all-files` (ruff, format, strict mypy, planning, callback matrix, manifest) | pass |
| `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass |

No Rack or Xvfb processes were left behind. The first full run had one failure: `tests/coverage/test_repository.py`
still allowed only the families bound before RP-16A. It now allows family 3,
and that file passes (26 tests).

What the actual-Rack tests establish:

- Three models added by a trusted entry module are created through the
  browser's path (`createModule`, `Engine::addModule`, `createModuleWidget`,
  `RackWidget`, an undoable `history::ModuleAdd`). The module menu's Delete
  (`ModuleWidget::removeAction`) removes one; `history->undo()` brings it back
  with the parameter value Rack saved when it was deleted.
- The null-module preview has the plain panel, 6 HP by 380 px, and the name
  from `plugin.json`.
- Rack's exit autosave records parameters and custom data. The next launch
  reloads them from that autosave: the parameter values and the `from_json`
  data come back.
- A factory that raises on reload is contained and diagnosed. Rack gets a
  placeholder, and the next exit autosave keeps that module's parameters and
  data unchanged.
- A `plugin.json` mismatch, a raising entry module, duplicate, empty and
  invalid slugs, a non-callable factory, adding twice and adding after
  initialization all leave no model half-registered, and Rack exits cleanly.

## Gates not established

The reopen uses the autosave Rack writes at exit, not a `.vcv` archive: the
archive writer is `patch::Manager`, which is outside `rack.hpp`. `.vcv`
save/reopen and plugin settings are RP-15D. Python module widgets (RP-28),
DSP (RP-17), macOS, Windows, the distributable build and sanitizers are not
covered.
