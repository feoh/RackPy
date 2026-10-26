# RP-18B validation (Linux x86_64)

Date 2026-10-26, on `main` after 58eff0c, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-18B (`tk-rackpy-18b-dsp-triggers-pulse-generator-timers-a-f72dae`) binds
the 57 records of `dsp/digital.hpp`. Reference page:
[docs/reference/dsp.md](../docs/reference/dsp.md#triggers).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --no-test` | builds, including the extended DSP reference |
| `uv run pytest -q tests/coverage tests/dsp tests/simd tests/docs` | 76 passed (the in-Rack DSP suite has 17 tests, 8 of them RP-18B's; the introspection check finds every RP-18B name in the built package) |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/dsp/in_rack_basic/test_digital.py`,
production build):

- **Reference streams.** `native/tests/dsp_reference.cpp` runs each Rack
  object over a whole stream on a fresh object, recording every result and
  state: 64 random bools through `BooleanTrigger.process` and
  `process_event`; 64 inputs (edges, NaN, infinities, random) through
  `SchmittTrigger` with three threshold pairs (one inverted); 31 vectors
  through `TSchmittTrigger<float_4>`; a trigger/step script through
  `PulseGenerator`, and its default duration; 32 steps of `Timer` and
  `TTimer<float_4>` (one negative); and `ClockDivider` at divisions 1, 3, 0,
  5 and `2**32 - 1`. Every stream replays identically.
- **Behaviour.** The first input after construction or reset does not
  trigger; NaN reaches neither threshold; the vector trigger resets to a full
  mask and its state is a live view; the deprecated `state` warns and maps
  through `s`; templates and aliases resolve, other arguments are refused;
  the pulse keeps a longer pulse and reports its state before a step; timers
  broadcast scalars; the clock divider's `uint32_t` limits and wrap-around.

The 18A, 18B and RP-20 suites ran again in the same command. The full suite
last ran for 18A (planning/rp-18a-validation.md). Only Linux x86_64 was
tested.
