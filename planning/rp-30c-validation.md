# RP-30C validation (Linux x86_64)

Date 2026-10-26, on `main` after 584af0c. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/services/test_settings.py` | 1 passed (6 tests inside Rack) |
| `uv run pytest -q tests/coverage` | 49 passed |
| `uv run pytest -q -m 'not dist and not inventory'` | 216 passed, 31 deselected (684 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run bin/generate_bindings.py check`, `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

The first full run had 2 failures:

- the introspection check, because `__bool__` was not a recorded protocol
  method (fixed in `src/rackpy/_introspect.py`);
- `tests/dsp/test_runtime.py::test_kernels_in_rack`, whose in-Rack result
  was not kept.

`test_kernels_in_rack` passed on its own (7 passed), and the full rerun
above passed. That suggests a load-dependent failure, but it is unconfirmed.

Inside actual Rack (`tests/services/in_rack_settings/test_settings.py`),
with each setting it changes restored afterwards:

- **Settings.**
  - Settings are read and written; float settings must be finite, and
    `thread_count` must be at least 1. `headless` is read-only.
  - Vec, colour lists, path lists, enums and `plugin_settings` (as Python
    data) round-trip, and `reset_cables` restores Rack's colours and labels.
  - `module_infos` can be assigned, and a `get_module_info` view writes to
    Rack's entry. A missing module gives `None`, and
    `is_module_whitelisted` works.
- **`prefer_dark_panels`.** A `ThemedSvgPanel` with light and dark SVGs
  switches to the dark one at its next `step()` after the setting changes,
  and its framebuffer is marked dirty.
- **`SharedMutex`.**
  - Relocking is refused ("deadlock"), as is unlocking a mutex the thread
    does not hold.
  - A writer excludes `try_lock` and `try_lock_shared` on another thread,
    and readers share the mutex across threads.
  - A `SharedLock` holds the mutex shared (a writer is excluded) and
    releases it exactly once.
  - Waiting for a mutex that a Python thread holds releases the GIL, so the
    holder runs and lets the mutex go.
- **`WeakPtr`.**
  - A pointer to a Python widget gives its weak count and handle (count 2
    with the view), and `__copy__` works.
  - Closing the widget clears every pointer and the handle's `ptr`.
  - An empty pointer is falsy, Rack's own scene widget can be pointed at,
    and `set(None)` clears a pointer.

Only Linux x86_64 was tested. Settings are saved by Rack when it quits; no
test checked the saved `settings.json`. Nothing in this slice runs on Rack's
audio path.
