# RP-22A validation (Linux x86_64)

Date 2026-10-26, on `main` after e3d67b4, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-22A (`tk-rackpy-22a-laboratory-bench-explicit-enablement--8e9285`) adds
the callback laboratory's Bench, explicit enablement and the Python
`process`/`processBypass`/`step` overrides: 7 records. Design and the
feasibility evidence: [rp-22-slices.md](rp-22-slices.md); reference:
[docs/explanation/callbacks.md](../docs/explanation/callbacks.md).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror` and `--test-hooks --build-dir build/hooks` | build; CTest 5/5 |
| `uv run pytest -q tests/laboratory tests/unit/test_errors.py` | 17 passed (the in-Rack laboratory suite in the production build at start-up, and in the test-hooks build with a live context) |
| `uv run pytest -q tests/coverage tests/engine tests/plugin tests/docs tests/unit tests/dsp/test_runtime.py tests/laboratory` | 117 passed (8 min 25 s), including the introspection check that every RP-22A name is in the built package |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/laboratory/in_rack/`):

- **Bench against Rack's engine** (live UI, test hooks): four native RackPy
  modules (a Python-written source, a `Sine` kernel, two `Gain` kernels, one
  with a bypass route) joined by five cables with a stacked input, 4-channel
  polyphony, non-finite source voltages and a feedback loop, bypassed for
  frames 30 to 49. Rack's engine was stepped one frame at a time with
  `Engine.step_block(1)` from a RackPy master module; a Bench ran the same
  patch. Every input and output of every module had the same channel count
  and voltages (compared by `repr`, so NaN too) after each of 96 frames.
  The first run of the in-Rack suite also passed this comparison.
- **Laboratory modules**: call order and `ProcessArgs` (rate, time, frame)
  per frame, one-frame cable delay and feedback, stacked/polyphonic/
  non-finite cables, bypass through Rack's routes or a Python
  `process_bypass`, `super()` to Rack's implementations (`process` calls
  `step`), the deprecated `step` warning, a Python gain equal to the native
  `Gain` kernel over 32 frames of 4 channels, errors stopping the frame and
  reaching the caller (not contained), nested callbacks ending in
  `RecursionError`, reentry refused, ownership refusals, Rack's event order
  for add/remove/cables/bypass/sample rate, and garbage collection of a
  module/instance/bench cycle (native references back to the start).
- **Refusals**: the laboratory is off at start-up; without `enable()` every
  laboratory call raises `LaboratoryError`; `rackpy.plugin.ModuleClass`
  refuses audio-thread overrides; `set_event_handlers` refuses the
  sample-rate event; a laboratory class has no factory; `Engine.add_module`
  and engine adoption refuse a laboratory module.

The first in-Rack run failed two of the new bench tests: they read the
channel count of unconnected outputs, which Rack keeps at 0. The tests were
fixed; the Bench was not changed.

Not measured here: laboratory callback cost (RP-22D). Only Linux x86_64 was
tested; macOS and Windows were not built.
