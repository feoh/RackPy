# RP-15D validation (Linux x86_64)

Date 2026-10-24, on `main` after RP-16 (f48d2dc). Pinned inputs: GCC 16.2.1,
uv 0.9.18, Rack SDK and Rack Free 2.6.6 (061ccf63), CPython 3.14.9,
pybind11 3.1.0. Host runs serialized with `flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 4/4 CTests pass; `rackpy-loader.json` has `"abi": 2` from the header |
| `uv run pytest -q tests/state` | 2 passed: the two-launch `.vcv` round trip and the in-Rack settings suite (3 tests) |
| `uv run pytest -q -m 'not dist and not inventory'` | 190 passed, 31 deselected (311 s), including the `plugin_settings.py` snippet inside Rack; no Rack or Xvfb processes left |
| `uv run pytest -q -m inventory`, `sphinx-build -W` | 29 passed; pass |
| `prek run --all-files` | pass |

The two-launch round trip, in windowed Rack under Xvfb:

1. Rack opens `patch.vcv`, a plain-JSON patch, from its command line. A
   module of the registered `PyStore` model, a `ModuleClass`, is added the
   way the browser adds it. It gets a parameter value, custom data and a
   `table.txt` in its patch storage.
2. It is deleted with the module menu's Delete. Undo restores it, with its
   state and its storage file.
3. Ctrl+S, sent through Rack's `EventState::handleKey`, makes Rack's patch
   manager save to `patch.vcv`. The file starts with zstd's magic number, so
   it is Rack's archive. Rack's exit writes
   `settings.json["pluginSettings"]["RackPy"]`.
4. The test deletes the autosave folder. The second launch opens
   `patch.vcv`, and Rack calls `settingsFromJson` with the stored settings.
   The parameter, the custom data and `table.txt` all come back from the
   archive, and the next exit stores the updated settings.

In Rack's live UI, the settings hooks as Rack calls them:

- with no handler, Rack gets NULL;
- values convert to and from JSON, and JSON null passes as `None`;
- `None` keeps the stored settings;
- errors, NaN and NUL are contained, with Rack keeping its stored value;
- non-callable handlers and other threads are refused.

## Gates not established

macOS, Windows, the distributable build and sanitizers are not covered. The
saves use Rack's own code paths through its input dispatcher, not a user
clicking the menu.
