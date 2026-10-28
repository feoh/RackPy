# RP-31C validation (Linux x86_64)

Date 2026-10-27, branch `rp31c` from fc4aa6d. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`; logs in `build/validation/` (not in git).

**Status:** The earlier session stopped at a WIP checkpoint; the continuation
below reran its validation on the committed source. No record was promoted;
all stay `implemented`.

## Commands run

| Command | Result |
| --- | --- |
| `uv sync --frozen` | ok |
| `bin/rackpy-dev build --werror --test-hooks` | first attempt failed: GCC 16 `-Wstringop-overflow` false positive on `Message::bytes.assign(ints)` in the new `messageOf` helper (dsp_reference.cpp); fixed by appending byte by byte. Rerun builds; 6/6 CTests pass |
| `uv run pytest -m unit -q` | 106 passed, 203 deselected |
| `bin/rackpy-dev typecheck` (strict mypy, 269 files plus bin/) | no issues |
| `uv run ruff check tests`, `uv run ruff format --check tests` | pass |
| `flock … uv run pytest -q tests/dsp/test_basic.py tests/values/test_values_in_rack.py tests/dsp/test_advanced.py` | 4 passed (DSP suite inside Rack; values suite inside Rack, test-hooks and production builds; DSP reference under ASan/UBSan) |
| `flock … uv run pytest -q tests/widget/test_tree.py::test_widget_live_ui` | 1 passed (widget suite inside windowed Rack under Xvfb, includes the new rotate test) |
| `uv run bin/generate_bindings.py generate`, then `check` | generated files match the manifest |
| `bin/planning-check` | 45 tasks, acyclic |
| `uv run bin/check_coverage.py check --without-built-package` | ok: absent 2, excluded_internal 111, implemented 2945 (Python names not checked against the built package) |

## Continuation validation (2026-10-28)

On `rp31c` at `bbd08d9`, with the pinned SDK and Linux x86_64:

- `uv run prek run --all-files`: passed, including strict mypy, manifest,
  callback and generated-file checks.
- `CMAKE_BUILD_PARALLEL_LEVEL=2 uv run bin/rackpy-dev build --werror
  --test-hooks`: passed, 6/6 CTests. A separate production build with
  `--build-dir build/production --werror` also passed 6/6 CTests.
- `uv run pytest -m unit -q`: 106 passed; `uv run pytest -m 'native and not
  host' -q`: 33 passed; `uv run pytest -m docs -q`: 17 passed.
- The targeted actual-Rack DSP, values, widget rotation and built-package
  introspection tests passed (8 outer tests), using `flock -n
  /tmp/rackpy-host-tests.lock`.
- `uv run sphinx-build -q -W -n -b html docs build/docs/html`, generator
  check and source-only coverage check passed. Introspection also checked
  the manifest against the production package inside actual Rack.

The pytest runs temporarily selected the existing production and test-hooks
build directories through `tests/conftest.py`; that tracked file was restored
immediately afterward. macOS and Windows were not tested.

## What changed

- **Values reference** (`native/tests/values_reference.cpp`): new cases for
  the Vec operators with a Vec and a float (`vec.op_*`), the in-place forms
  (`vec.op_i*`), Rect `==`/`!=` (`rect.op_eq`, `rect.op_ne`) and Rack's Rect
  aliases (`rect.is_containing`, `rect.is_intersecting`, `rect.is_equal`).
- **Values replay** (`tests/values/in_rack/test_reference_replay.py`):
  `vec.plus`, `vec.minus`, `vec.mult_v`, `vec.div_v` now call the methods
  (`a.mult(b)`, …), not the operators; the operators have their own cases.
  In-place cases fail unless the result is the left operand itself.
- **DSP reference** (`native/tests/dsp_reference.cpp`) and replays:
  - biquad types and MidiParser modes are passed **by name**
    (`getattr(Type, name)` in Python), and `biquad_types`/`biquad_types4` and
    `parser.enums` compare every Python enumerator's name and value, in order,
    with the header's;
  - MidiParser state now also records `pw_filters`/`mod_filters` (out,
    lambda), `mono_mode`, `poly_mode`, `retrigger_on_resume`,
    `release_velocity_enabled`, `pw_range`, `smooth`, `clock_division`;
  - three new scripts (`parser.16.methods_poly`, `methods_mono`,
    `methods_mpe`) call `press_note`, `release_note`, `assign_channel`
    (result recorded), `refresh_held`, `press_pedal`, `release_pedal`,
    `process_cc`, `process_system`, `set_filter_lambda`, `set_*_mode`, and
    write `held_notes`, `pedal`, `pw_range`, `rotate_index` directly;
  - the MIDI generator records `key_pressures` and `channel_pressure` after
    every op;
  - smoothers record `rise_lambda`/`fall_lambda`; the convolver records
    `input_ffts`; decimators and upsamplers record `in_buffer`; a new
    `src.2.refresh` case assigns `in_rate`/`out_rate` and calls
    `refresh_state()` before processing.
- **Behavioural tests** added to existing in-Rack tests: Vec methods with a
  Vec (`test_vec_operators_are_racks`), a false Rect `==` and `!=`
  (`test_rect_fields_are_live_views`), ExponentialSlewLimiter lambda fields
  (`test_single_pole_filters`), generator pressure dedup through the fields
  (`test_midi_generator`), parser refusals through `release_note`,
  `process_cc`, `process_system` (`test_midi_parser_refuses_what_rack_would_get_wrong`),
  `input_ffts` (`test_real_time_convolver`), `refresh_state`
  (`test_sample_rate_converter`), `in_buffer` (`test_decimator_and_upsampler`),
  and a new `test_transform_rotate_without_an_origin_is_racks_rotation`.
- **Docs**: `values.md` (Rect's methods, aliases and operators; Vec `mult`/`div`
  overloads), `dsp.md` (ExponentialSlewLimiter fields, biquad values,
  generator fields, MidiParser enumerators, fields and methods, inverse FFTs,
  `in_buffer`).
- **Manifest**: tests re-cited for the in-place Vec operators, Rect `==`/`!=`,
  Vec `mult`/`div(Vec)` and six biquad enumerators; the new rotate test cited
  by both `TransformWidget::rotate` records. Generated files regenerated.

## Per-record table

Chunk `dsp_simd_math` records with weak/missing tests or missing docs (74),
plus the three P1-2 overloads. "Replay" means a differential case against
Rack's own code compiled by the reference program.

| Record(s) | Evidence now |
| --- | --- |
| `MidiParser::MonoMode::*` (5), `PolyMode::{ROTATE,REUSE,RESET,NUM_POLY}_MODE` (4) | `parser.enums` checks name and value of each; scripts set modes by name (`test_midi.py::test_every_reference_script_matches_rack`); docs name each enumerator |
| `TBiquadFilter::Type::{BANDPASS,HIGHPASS_1POLE,HIGHSHELF,LOWPASS_1POLE,LOWSHELF,NOTCH}` | `biquad_types[4]` name/value table; every stream sets its type by name (`test_filters.py::test_every_reference_stream_matches_rack`); re-cited to that test only |
| `Decimator::inBuffer`, `Upsampler::inBuffer` | replay records `in_buffer`; `test_decimator_and_upsampler` asserts its contents; docs added |
| `MidiGenerator::channelPressure`, `keyPressures` | replay records both after every op; `test_midi_generator` writes them and checks the setters' dedup |
| `MidiParser` fields `heldNotes`, `modFilters`, `monoMode`, `pedal`, `polyMode`, `pwFilters`, `pwRange`, `pws`, `releaseVelocityEnabled`, `retriggerOnResume`, `rotateIndex` | parser state records all; methods scripts write `held_notes`, `pedal`, `pw_range`, `rotate_index`; docs list the fields |
| `RealTimeConvolver::inputFfts` | replay records it; `test_real_time_convolver` checks size, zeroing and the block written |
| `TExponentialSlewLimiter::riseLambda`, `fallLambda` | `smoothers[4]` replay records them; `test_single_pole_filters` sets, reads and assigns them; docs added |
| `Rect operator==`, `operator!=` | `rect.op_eq`/`op_ne` replay; false cases in `test_rect_fields_are_live_views` (re-cited) |
| Vec `operator+= -= *= /=` (6) | `vec.op_i*` replay (identity checked); re-cited to `test_vec_in_place_operators_change_the_value_itself` and the replay |
| `ComplexFFT::ifft`, `ifftUnordered`, `RealFFT::irfft`, `irfftUnordered` | tests already existed (`rfft.*`/`cfft.*` replay calls each inverse); docs now name them and their input order |
| `MidiParser::assignChannel`, `pressNote`, `pressPedal`, `processCC`, `processSystem`, `refreshHeld`, `releaseNote`, `releasePedal`, `setFilterLambda`, `setMonoMode`, `setPolyMode`, `getWheelChannels` | methods scripts call each from Python (wheels recorded in state); refusal tests for `release_note`, `process_cc`, `process_system`; docs added |
| `SampleRateConverter::refreshState` | `src.2.refresh` replay; `test_sample_rate_converter` checks the state is rebuilt only by it |
| Rect `expand`, `fromCorners`, `fromMinMax`, `getTopLeft/TopRight/BottomLeft/BottomRight`, `interpolate`, `intersect`, `intersects`, `nudge`, `shrink`, `zeroPos` | tests already existed (replay); docs sentence listing Rack's Rect methods |
| Rect `isContaining`, `isEqual`, `isIntersecting` | new replay cases; docs name the aliases |
| `Vec::mult(Vec)`, `Vec::div(Vec)` (P1-2) | replay now calls the methods; `test_vec_operators_are_racks` calls them; re-cited |
| `TransformWidget::rotate(float)` (P1-2) | `test_transform_rotate_without_an_origin_is_racks_rotation`: translate/rotate/translate equals Rack's `rotate(angle, origin)` bit for bit, and `rotate(angle)` from a non-identity matrix is nvgTransformPremultiply by the rotation (approx) |

## Open issues

- The rotate test has no compiled C++ reference: the exact comparison is with
  Rack's own `rotate(angle, origin)` composition, the closed form is
  approximate (1e-5).
- `Vec::plus`/`Vec::minus` had the same operator routing in the replay; they
  are fixed too, but their records (citing the replay) were not edited.
- Only Linux x86_64 was tested.
