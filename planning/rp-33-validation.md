# RP-33 validation: the tutorial oscillator and the plugin template

## What was built

- `bin/rackpy-plugin` (cyclopts): `create` makes a plugin project
  (`plugin.json`, `rackpy-plugin.json`, the entry package, `res/`,
  `.gitignore`); `add-module` adds a module and, from an SVG panel's
  `components` layer, generates its Python (enums, `configure()`, a
  `ModuleWidget` with Rack's component library) and adds the model to the
  entry package; `build` combines a project with a RackPy bundle into a plugin
  directory under the project's own slug. The parser and generator are derived
  from Rack 2.6.6's `helper.py` (GPL-3.0-or-later) with attribution; seven
  intentional changes, including a corrected rect-size bug in `helper.py`, are
  listed in `examples/template/PROVENANCE.md` and in the licensing errata.
- `examples/template`: exactly the tool's output for an original 6 HP panel
  with one of each component kind.
- `examples/oscillator`: an original plugin with the tutorial module's
  feature. The DSP is a `rackpy.dsp.graph` graph in a module that does not
  import Rack (`rackpyoscillator_dsp.py`), compiled per module to a native
  kernel; the same module has the labelled pure-Python offline reference.
- `native/tests/oscillator_reference.cpp` (CMake target
  `rackpy_oscillator_reference`): the oscillator as a C++ plugin author writes
  it, on Rack's own `Param`/`Input`/`Output`/`Light`, in Rack's FP mode.
- `upstream/examples.json`: a `disposition` (how, files, tests) for each of
  RP-33's 16 samples; `tests/examples/test_dispositions.py` checks that every
  named file and test function exists.
- A facade fix found on the way: `rackpy.dsp.graph` kept `IntEnum` members
  themselves in plan data when used as indexes; `_index` now stores plain
  numbers (with a test).

## Evidence (Linux x86_64, Rack Free 2.6.6, this worktree's builds)

- `bin/manual-sources capture && check` on 2026-10-31: all 69 recorded manual
  code blocks match their hashes. `helper.py` is the pinned, hashed file.
- Fresh hooks and production builds with warnings as errors: 7/7 CTests each.
- `tests/examples` unit and native (20): the template regenerates byte for
  byte; the tool's refusals (bad slug, reserved or stdlib entry names,
  existing directory, missing licence, missing layer, duplicate names, panel
  outside the project, overwrite) and geometry (view-box offset, unoffset
  sizes, SVG pixels at 75 DPI); the offline reference and the C++ reference
  each pass the signal checks at 44.1, 48 and 96 kHz over both fixed streams,
  and agree within 0.05 V with identical lights.
- `tests/examples` host (4): the in-Rack suite (11 tests: controls and display
  scaling, C++ parity at three rates and two streams, Initialize, a sample-rate
  change, the browser preview, Rack's engine and an engine cable); the
  two-launch windowed session (a patch of two oscillators and a cable carries
  the sine, browser add, delete, undo, autosave, reopen with knob, cable and
  the third module); the template and the oscillator built on a production
  bundle load under their own slugs, the oscillator's two-module, one-cable
  patch surviving two launches.
- Measured in Rack, the module's largest difference from the C++ reference
  over one second: 44.1 kHz 0.0051 V (unpatched) and 0.0004 V (patched);
  48 kHz 0.0093 and 0.0110 V; 96 kHz 0.0140 and 0.0054 V. The bound is
  0.05 V. These equal the offline reference's differences to six decimals.
- Whole unit and native selection: 200 passed. Host: the examples, the in-Rack
  graph and kernel-library suites and every docs snippet, 8 passed.
- prek (mypy over 335 files including `examples/`, strict pyright now including
  `examples/`, the facade snapshot), Sphinx `-W -n`.

## Calibrations and findings

- The C++ and graph oscillators keep phase in [-0.5, 0.5) and [0, 1); their
  float rounding differs, so they drift apart by about 0.014 V a second at
  most here, with the same frequency. Documented in the example's README.
- The blink comes from summing a float32 sample time, 3 frames early at
  44.1 kHz and 31 frames (0.065%) at 48 kHz over one second, identically in
  C++; a first one-frame tolerance failed on this, so the check allows 0.2%.
- A first engine test expected an unpatched output to report one channel;
  Rack's `Port::setChannels` keeps it at 0. The test now connects a cable.
- `_testing.module_model` returns the model slug and whether the module is the
  failure placeholder; a first assertion misread it.
- Rack 2.6.6 has no "Open user folder" menu item; the README gives the Linux
  path the source documents instead.

## Limitations

- Linux x86_64 only; macOS and Windows are RP-40 and RP-41.
- The production load tests use a fresh Rack user folder on the build machine,
  with the bundle's runtime symlinked into the development cache. A clean
  machine and a copied runtime are RP-39's.
- Headless Rack does not build module widgets; widgets are covered by the
  hooks build's windowed and in-Rack tests.
- Only one RackPy plugin can run per Rack process (ADR 002); the template
  documents it. How plugin authors should license their plugins remains an
  open owner question in `docs/licensing.md`.
