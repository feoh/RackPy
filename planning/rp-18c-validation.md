# RP-18C validation (Linux x86_64)

Date 2026-10-26, on `main` after e8ecb80, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-18C (`tk-rackpy-18c-dsp-filters-rc-exponential-peak-slew--7c6985`) binds
the 78 records of `dsp/filter.hpp`. Reference page:
[docs/reference/dsp.md](../docs/reference/dsp.md#filters).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --no-test` | builds (the 98 IIR instantiations take `filter.cpp` about 75 s) |
| `uv run pytest -q tests/coverage tests/dsp tests/simd tests/docs` | 76 passed: the in-Rack DSP suite has 25 tests (8 RP-18C ones, and the call-cost measurement); the introspection check finds every RP-18C name in the built package |
| `RACKPY_CALL_COST_OUT=... uv run pytest -q tests/dsp/test_basic.py` | wrote [benchmarks/rp18-dsp-call-cost.json](../benchmarks/rp18-dsp-call-cost.json) |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/dsp/in_rack_basic/test_filters.py`,
production build):

- **Reference streams.** For `float` and `float_4`: RC filters at four
  cutoffs (one 0, giving `c = inf`) over a 64-sample stream with NaN and
  infinity, recording both outputs and `c`; the four smoothers at three
  settings; the deprecated one-argument forms with `set_rate` and `peak()`;
  IIR filters of orders (2, 2), (3, 3), (4, 2), (2, 5), (8, 8) for float and
  (3, 3), (5, 4) for vectors, with random coefficients, impulse and stream
  outputs and transfer functions, responses and phases at five frequencies;
  every biquad type, `NUM_TYPES` included, at four parameter sets, with
  coefficients, 16-sample impulse responses and magnitudes; and a new biquad.
  Every stream replays identically. (The first run differed in four vector
  streams; the reference had recorded the wrong lane pattern for their
  inputs, and was fixed.)
- **Behaviour.** Templates, aliases and `IIRFilter[B, A, T]` with refused
  orders; snapping, jumping and slewing; live fields and array views that
  keep their filter alive; deprecated forms warning; `set_coefficients`
  lengths; complex transfer functions; NaN coefficients of a new biquad;
  `NUM_TYPES` setting nothing; the enum shared by both instantiations; and
  `process_block` equal to per-call processing, in place included, with its
  refusals.
- **Cost** (diagnostic, loaded desktop, `powersave`): a Python loop of
  `process` calls 67 to 72 ns per sample, `process_block` 2.7 to 3.3.

Only Linux x86_64 was tested.
