# RP-18A validation (Linux x86_64)

Date 2026-10-26, on `main` after af3c6e0, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-18A (`tk-rackpy-18a-dsp-scalar-helpers-approximations-con-fd4045`) binds
the 53 records of `dsp/common.hpp`, `approx.hpp`, `convert.hpp`,
`window.hpp` and `ode.hpp`. Reference page:
[docs/reference/dsp.md](../docs/reference/dsp.md).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --no-test` and `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | build; 5/5 CTests pass |
| `uv run pytest -q tests/dsp/test_basic.py tests/simd` | 3 passed (the in-Rack DSP suite has 9 tests) |
| `uv run pytest -q -m 'not dist and not inventory'` | 218 passed, 1 failed: the introspection did not record `__int__` (`Int24`'s conversion); fixed in `src/rackpy/_introspect.py`, then `uv run pytest -q tests/coverage` 49 passed |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, 114 dependencies, acyclic |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/dsp/in_rack_basic/test_basic.py`, production
build):

- **Reference replay.** `native/tests/dsp_reference.cpp` runs Rack's code on
  4,143 cases (edge and seeded random inputs; every `N` from 1 to 32 for the
  polynomials; every `convert` pair; windows of length 0 to 64; 100-step ODE
  trajectories for an oscillator and a damped system, float and float_4).
  Every case matches; 28 scalar cases with subnormal inputs are skipped and
  counted (Python cannot pass a subnormal float into Rack). Both sides call
  Rack through `native/bindings/dsp/calls.hpp`: inlined at two call sites,
  `polyDirect` and `polyEstrin` had differed by several ULPs, and once by
  `-inf` against NaN.
- **Behaviour.** float and float_4 dispatch and broadcasting; coefficient
  counts 0 and 33 refused; `exp2_floor`/`exp2_taylor5` refusing Rack's
  undefined domain for floats only; deprecated aliases warning; `Int24`
  truncation and overflow; the `convert` template (ctypes keys, undefined
  pairs, int-only samples, buffer item types, short, read-only and
  overlapping buffers); in-place windows with length checks; ODE callbacks on
  copies (read-only `x`, zeroed `dxdt`, call times, exceptions leaving `x`
  unchanged, `len` limits, float_4 vectors updated in place); `Frame`
  specializations, views, buffer layout and refusals.

**Owner review item:** the `poly*` contract (`N` from 1 to 32).

Only Linux x86_64 was tested.
