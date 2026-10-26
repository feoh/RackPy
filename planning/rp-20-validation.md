# RP-20 validation (Linux x86_64)

Date 2026-10-26, on `main` after 078257b, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-20 (`tk-rackpy-20-expose-simd-vectors-and-functions-with-563eec`) binds
148 records: `simd/Vector.hpp`, `simd/functions.hpp`, `simd/common.hpp`,
`sse_mathfun.h`, `sse_mathfun_extension.h` (143, family 6; 2 macros are
absent on lin-x64) and `Port`'s 5 SIMD method templates (family 2). The
reference page is [docs/reference/simd.md](../docs/reference/simd.md).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --no-test` and `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | build, including both SIMD references; 5/5 CTests pass |
| `uv run pytest -q tests/simd` | 2 passed: the in-Rack suite (12 tests) and the SSE4.2/SIMDe comparison |
| `uv run pytest -q -m 'not dist and not inventory'` | 218 passed, 31 deselected (678 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `bin/planning-check` | 45 tasks, 114 dependencies, acyclic |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/simd/in_rack/test_simd.py`, production build):

- **Reference replay.** `native/tests/simd_reference.cpp` evaluates Rack's
  code on 23,985 cases over 148 functions and operators (edge lanes:
  signed zeros, subnormals, `±inf`, NaN, `2**31`, integer extremes, every
  mask; plus seeded random lanes). Every case matches lane for lane: exact
  bits for bitwise logic, masks, casts, loads and stores, and with zero signs
  and NaN payloads ignored for arithmetic (ADR 009). Vectors are rebuilt from
  bit patterns. 196 scalar cases with subnormal inputs are skipped and
  counted: inside Rack, converting a Python float to a C++ float flushes a
  subnormal to zero, so Python cannot pass one (tested).
- **Python rules.** Zeroed default construction, broadcasting from both
  sides, the refusal of `float_4 + int32_4`, int-only `int32_4` lanes with
  `OverflowError`, checked lane indices, `s` as a live view that keeps its
  vector alive, `v` as a register copy, no truth value and no hash, in-place
  compound operators, `++`/`--` methods, and logical shifts with counts
  outside 0 to 31.
- **Buffers.** Loads and stores at any alignment and through slices; short,
  strided, wrong-type and read-only buffers fail before Rack touches memory.
- **Templates.** `Vector[float, 4] is float_4`, other arguments refused;
  `movemask_inverse[float_4]`; `pow<T>(T, int)` refusing exponents from
  `2**30` on (Rack's loop overflows).
- **Ports.** All 5 SIMD methods for `T = float_4`, with connected,
  monophonic and polyphonic ports, and `first_channel` outside 0 to 12
  refused.

**Rack 2.6.6 findings** (documented in the reference):

- `simd::log(x)` is NaN for `x <= 0`, zero included; `simd::exp` saturates
  at about `2.406e38`, also for `inf` and NaN; so `simd::pow(a, b)` for
  `a <= 0` returns about `2.406e38`, `pow(0, 1/3)` included.
- `simd::sin(±inf)` is `∓inf`.
- `Vector::load`'s comment says the lane order is reversed on little-endian
  machines; `_mm_loadu_ps` puts `x[0]` in lane 0.
- `simd::atan2(x, y)` names its numerator `x`; it is `std::atan2(x, y)`.

**SSE4.2 against SIMDe** (`tests/simd/test_simde.py`): the reference is also
built through `simd/common.hpp`'s SIMDe branch with SIMDe's portable code
(`-mno-sse3 -DSIMDE_NO_NATIVE`). They agree exactly on integer, bitwise,
mask, comparison, conversion, load and store results. The pinned envelope
covers zero signs, NaN payloads, subnormal lanes, sse_mathfun's last bits
(at most `1e-5` relative), `sin(±inf)` (SIMDe gives NaN), `rcp` (within
`1.5 * 2**-12`), and `rsqrt`, which SIMDe's portable code computes with a
bit trick that is wrong for 0, negatives and infinity.

**Not tested:** arm64 (Rack's SIMDe NEON path), macOS and Windows. Only Linux
x86_64 was built and run. DSP templates over `float_4` are RP-18; graph
kernels are RP-21. C++ kernel libraries can already use `rack::simd`.
