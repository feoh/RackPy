# RP-22C validation (Linux x86_64)

Date 2026-10-26, on `main` after 698e22b, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-22C (`tk-rackpy-22c-laboratory-quantity-value-methods-rec-c2b64e`) gives
the mixed `Quantity`/`ParamQuantity` value and range methods and
`midi::OutputDevice::sendMessage` their laboratory Python path: 5 records
implemented, 6 reconciled. Decisions: [rp-22-slices.md](rp-22-slices.md);
explanation: [laboratory quantities](../docs/explanation/callbacks.md#laboratory-quantities).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror` and `--test-hooks --build-dir build/hooks` | build; CTest 5/5 |
| `uv run pytest -q tests/coverage tests/engine tests/values tests/io tests/plugin tests/unit tests/native tests/laboratory` | 109 passed, 1 failed: `tests/coverage/test_repository.py` pinned the five ParamQuantity records as the only ones left open for RP-22; rewritten as `test_quantity_records_are_mapped_with_their_laboratory_path` (26 passed afterwards) |
| `uv run pytest -q tests/docs` | first 2 failed (a link to a `(label)=` target, which MyST page links do not resolve; changed to the heading anchor), then 17 passed |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run bin/check_coverage.py check --without-built-package`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html`, `bin/planning-check` | pass, 0 warnings |

The coverage check also found that the five formerly `planned` records
cited a documentation anchor that did not exist
(`engine-module.md#unattached-standalone-quantities`, never checked while
they were planned); they now cite `#standalone-quantities`.

**Inside actual Rack** (`tests/laboratory/in_rack/test_quantities.py`):

- **Differential**: a `rackpy.laboratory.Quantity` written in Python with
  `ValueQuantity`'s semantics (bounds, `clamp_safe`) and the native
  `rackpy.types.ValueQuantity` gave the same results and values through 21
  of Rack's own `Quantity` operations (display value and string, scaled
  values, `set_min`/`set_max`, `toggle`, `move_*`, `reset`, NaN), which
  reached the Python `set_value`/`get_value` at least 10 and 15 times.
- **Parameter quantities**: Rack's `ParamQuantity::getDisplayValue` and
  `SwitchQuantity::getDisplayValueString` from Python values; an ordinary
  module refuses a laboratory quantity through `config_param` and raw slot
  assignment, a laboratory module takes it; in live Rack, Rack's `onReset`
  (Python default), `paramsToJson` (Python value) and `paramsFromJson`
  (clamped to Python's maximum) on a laboratory module on a Bench.
- **Rack threads** (test hooks): reading a laboratory quantity from a
  thread with a Rack context ran no Python, counted a refusal, and Rack's
  implementation answered.
- **MIDI**: a Python `MidiOutputDevice` connected to a Python-made
  `midi.Output` with channel 3 received Rack's channel-applied bytes, also
  from a laboratory module's `process` on a Bench; an override's error
  reached the caller; deleting the device left the port pointing nowhere.
- Errors raised in an override reach the Python caller through Rack's code;
  the laboratory classes refuse creation until `enable()`; ordinary
  subclasses that define the value methods are still refused.

Only Linux x86_64 was tested; macOS and Windows were not built.
