# RP-19C validation (Linux x86_64)

Date 2026-10-26, on `main` after 765adcb, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock` (shared with the parallel RP-22 worker).

RP-19C (`tk-rackpy-19c-dsp-ring-buffers-ringbuffer-doublerin-9d17ea`) binds
the 43 records of `dsp/ringbuffer.hpp`. Reference page:
[ring buffers](../docs/reference/dsp.md#ring-buffers).

| Command | Result |
| --- | --- |
| `cmake --build build/dev` | builds without warnings: 165 instantiations in three translation units; `rackpy_dsp_reference_sanitized` (ASan + UBSan) included |
| `uv run pytest -q tests/coverage tests/docs tests/unit tests/dsp` | 99 passed and 2 docs failures (an ambiguous `S` cross-reference, below); after the fix `tests/docs` 17 passed and `tests/dsp/test_basic.py tests/dsp/test_advanced.py` 2 passed. The in-Rack wrapper asserts every `in_rack_basic/test_*.py` file reported, `test_ringbuffer.py` included; the sanitized reference (4,342 cases) runs clean, leak detection on |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |
| `uv run bin/check_coverage.py check --without-built-package` | implemented 2919, planned 10, unreviewed 16 |

The sanitized run found one bug, in the new reference itself (an empty
`memcpy` from a null pointer), fixed before the bindings' tests ran.

**Inside actual Rack** (`tests/dsp/in_rack_basic/test_ringbuffer.py`,
production build):

- **Reference cases.** Nine scripts, each recording the returned elements,
  `size()`, `empty()`, `full()`, `capacity()`, `start` and `end` after every
  step and the data at the end: `RingBuffer<T, 16>` for float, float_4 and
  `Frame<2>` (wrapping bulk copies, overrun, underrun, `n` = 0 and `S`),
  `DoubleRingBuffer<T, 16>` for float, float_4 and `Frame<8>` (wrapping
  appends through `endData`, reads through `startData`, overrun, underrun,
  `startIncr` past `end`), and `AppleRingBuffer<T, 16, 256>` for float,
  float_4 and `Frame<16>` (pushes and appends that move the window back, past
  `S`). Every step replays identically.
- **Behaviour.** The generic contract's template set and its refusals,
  `Frame[C]` keys, zeroed data, wrapped `size()`/`capacity()`, index wrap at
  2^64, bulk copies past `S` and short or mistyped buffers refused, refused
  copies of the atomic-indexed buffers, read-only `start_data` views
  (vectors read as copies), views that keep their buffer alive, and every
  `AppleRingBuffer` refusal (field writes, `push` with a full block, `shift`
  when empty, `end_data`, `end_incr`, `start_incr`), with plain-data copies.

**Owner review item:** the generic contract (T = float, float_4, `Frame<2>`,
`Frame<8>`, `Frame<16>`; S = 16 to 32768, and 16 to 4096 with N = 16 S for
`AppleRingBuffer`) is RackPy's proposal for coverage policy 10, recorded in
the manifest and [docs](../docs/reference/dsp.md#ring-buffers).

Docs fix: `rackpy.raw.engine`'s stub named the PEP 695 type parameter of
`config_switch` and `config_button` `S`, which the new `S` class attributes
made an ambiguous cross-reference; it is now `Q`.

Only Linux x86_64 was tested.
