# RP-34 validation: configuration, lifecycle, state and context-menu examples

Task: `tk-rackpy-34-port-guide-configuration-lifecycle-sta-f25a4c`, branch `rp34`.
Machine: Intel i9-14900K, Linux 7.3.0-4-cachyos x86_64, GCC 16.2.1, Python 3.14,
Rack Free 2.6.6 (`061ccf63c1758599396ac1bb10d47345d9d34076`).

## What was delivered

- `examples/configuration/`: Shaper (each parameter display, a labelled
  switch, a button, descriptions, port and light names, bypass routes, three
  factory presets) and Splitter (a custom bypass).
- `examples/state-and-events/`: Sequencer, a `ModuleClass` with versioned
  JSON state, a patch-storage pattern file, plugin settings, `on_reset` and
  `on_randomize`; `laboratory/events_on_a_bench.py` for every event in Python,
  offline.
- `examples/menus/`: Switchboard, whose context menu uses every menu helper.
- `tests/examples/test_controls_state.py`, with `in_rack_controls/` and
  `entries/controls_state_session.py`.
- A binding the custom-bypass sample needed: `Module.set_bypass_kernel`,
  `bypass_kernel` and `bypass_kernel_failed` (`native/module/module.hpp`'s
  `processBypass` override, `native/bindings/dsp/kernel.cpp`,
  `native/bindings/engine/module.cpp`). Rack's engine runs the bypass kernel
  for bypassed frames instead of copying routes; without one, Rack's own
  `processBypass` runs as before. The manifest's `processBypass` record notes it
  (status unchanged: implemented).
- `native/tests/controls_reference.hpp`: the two configuration modules as C++,
  run inside Rack through the hooks build's `_testing.controls_reference()`.
  Rack's `ParamQuantity` reaches parameters through `APP->engine`, so a
  standalone program cannot produce its strings; the first, standalone attempt
  crashed in `ParamQuantity::setValue`.
- `bin/rackpy-plugin build` now copies a project's `presets/`.
- `rackpy.raw.create_menu_label` gained overloads so pyright infers
  `MenuLabel` when `label_type` is omitted.
- `_testing.module_process_bypass` takes a sample rate and frame and refuses
  engine-owned modules, as `module_process` does.
- Dispositions for all 22 RP-34 samples in `upstream/examples.json`;
  `tests/examples/test_dispositions.py` now requires them for RP-33 and RP-34.

## Sample coverage

12 ports (`guide-02` to `guide-12`, `migrate2-06`), 4 supporting fragments
(`guide-01`, `migrate2-04`, `migrate2-05`, `migrate2-09`), 3 config samples
(`presets-01`, `-03`, `-04`, now real factory presets rather than
documentation only) and 3 duplicates (`migrate2-08`, `-10`, `-11`). Every one
has a disposition naming existing files and test functions.

Intentional corrections of the manual (also in the examples' PROVENANCE):
`guide-08`'s `onRandomize(const ResetEvent&)` is written with `RandomizeEvent`;
`guide-10` vs `migrate2-11` (get vs create the storage directory in `onAdd`)
is resolved as read-only in `on_add`, create in `on_save`.

No asynchronous replacement changes synchronous semantics: `on_reset`,
`on_randomize`, `to_json`, `from_json`, `on_add`, `on_save` and the menu
callbacks run synchronously on Rack's main thread, as in C++. The sample-rate
event is the one Rack sends on audio threads; it is handled natively by the
kernel (no Python), and the laboratory script shows a Python handler offline.

## Evidence

All on fresh builds of this branch: `build/dev` (`--werror --test-hooks`) and
`build/production` (`--werror`), each `100% tests passed out of 7` (CTest).

| Check | Result |
| --- | --- |
| Full pytest (both builds, Linux host and GUI tests) | 413 passed in 868.84 s |
| `tests/examples/` | 52 passed (11 in `test_controls_state.py`) |
| `tests/docs tests/examples tests/coverage tests/facade` | 168 passed |
| Native kernel tests (`rackpy_kernel_tests`, incl. `testBypassKernel`) | all checks passed |
| Audio guard, 100 000 bypassed frames (route copy, then bypass kernels swapped and controlled), production build | malloc 0, free 0, calloc 0, realloc 0, aligned 0, mutex_lock 0, write 0, gil_ensure 0 |
| `prek run` (incl. pyright facade contract, facade surface snapshot) | passed |
| `uv run mypy` (strict, 349 files) | no issues |
| `sphinx-build -W -n` | clean |
| `bin/manual-sources check` | 69 code blocks match |
| `bin/check_coverage.py check --without-built-package` | absent 15, excluded_internal 111, implemented 3098 (unchanged) |

Measured differences between the configuration modules and their C++
reference, inside Rack, at 48 kHz over 2400 frames (the test allows 1e-5 V):

| Scenario | Largest difference |
| --- | --- |
| Shaper defaults | 4.99e-09 V |
| Shaper rectified tremolo | 4.99e-09 V |
| Shaper positive half, muted a while | 4.69e-09 V |
| Shaper polyphonic (3 channels) | 9.57e-07 V |
| Shaper unpatched | 5e-10 V |
| Shaper bypassed routes (3 channels) | 5e-09 V |
| Splitter level | 4.99e-09 V |
| Splitter polyphonic (4 channels) | 5e-09 V |
| Splitter custom bypass (4 channels) | 5e-09 V |

Every control's name, label, unit, description, range, default, snap and
smooth flags, switch labels, and value, display string and tooltip at five
or six values match the C++ module exactly; so do the ports' and light's
names, full names and descriptions, and the bypass routes.

Round trips in actual Rack (windowed, two launches through Rack's autosave):
valid, newer-version and missing module data; good, wrong-size and absent
pattern files in patch storage; plugin settings in `settings.json`; bypassed
modules; changes from launch 1 (range, randomized pattern and length, plugin
default length, switchboard pitch) saved to the patch, its storage and
`settings.json`, and restored in launch 2. Each example also builds with
`bin/rackpy-plugin build` on a production bundle and loads under its own slug
over two launches.

Laboratory script: events `rate 48000, reset, rate 96000, randomize`; the
glide reaches 0.63212 at 48 kHz and 0.63250 at 96 kHz after 10 ms.

## Limitations

- Linux x86_64 and Rack Free 2.6.6 only. macOS and Windows are not built or
  tested here.
- The menu items were driven through Rack's menu classes (`create_menu`,
  `do_action`, `on_enter`), not by a person clicking. With the widget in
  Rack's rack, Rack's own `ModuleWidget::createContextMenu` was checked to
  call the widget's `append_context_menu` after Rack's items (its labels run
  from the model name to the example's "Pitch").
- Patch round trips use Rack's autosave, not File > Save to a `.vcv` (which
  packs patch storage; `tests/services` and RP-15 cover that path).
  Duplicating a module (Rack copies patch storage after `onSave`) is not
  tested for these examples.
- Initialize, Randomize and save were triggered through the hooks build's
  engine calls (`engine_reset`, `engine_randomize`, `engine_prepare_save`),
  which call Rack's `Engine` methods, not through Rack's menus.
- Only one RackPy plugin runs per Rack process, so the examples cannot be
  installed together.
