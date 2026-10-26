# RP-24A validation (Linux x86_64)

Date 2026-10-26, on `main` after c5331b4. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | 205 passed, 31 deselected (578 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/engine/ops tests/docs` | 19 passed |
| `uv run bin/generate_bindings.py check` | generated files match |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |
| `bin/planning-check` | 45 tasks, acyclic |

In actual Rack's live UI with Rack's engine running
(`tests/engine/ops/in_rack/test_engine.py`):

- `Context.engine` is one handle per context. Frames advance from Rack's own
  thread. The block and meter values are readable. A suggested sample rate
  of 48000 applies and is restored, and NaN is refused.
- The RuntimeProbe (a C++ module) is an `EngineModule`. It is found by ID,
  equal and hashable. Its JSON round trips. Bypass, randomize, reset and the
  save events run, and it appears in `to_json`.
- Params: raw and smooth values, an out-of-range ID refused, a detached
  module's raw value set, and smoothing a detached module refused.
- The master module can be set to the probe and restored.
- Refused: `clear`/`from_json` while the rack has widgets, `remove_module`
  of another plugin's module, `add_module` of a module without a model, and
  `step_block` without a RackPy master module.
- The documentation's snippet (`docs/snippets/engine.py`) runs.

Windowed Rack (`tests/engine/ops/test_engine_ops.py`), with a Python model:

- `Model.create_module` then `add_module`: the module is in the engine, has
  an ID, `get_module` returns its own handle, and its JSON names the model.
  Adding it again is refused.
- With a control pointing at it, `remove_module` is refused.
- `reset_module` runs its `on_reset` handler. An engine call from there
  raises `EngineLockError` (re-entry would deadlock), and the param is back
  at its default.
- As master module, `step_block(64)` then `step_block(32)` advance the frame
  by exactly 96. The last block is 32 frames.
- `remove_module` gives the module back to Python. It is out of the engine,
  still alive, and closes.
- A module added through Rack's browser is owned by its widget, so
  `remove_module` refuses it.

Not covered: cables, parameter handles, history and other plugins' models
(RP-24B to 24D). Only Linux x86_64 was tested.
