# RP-19A validation (Linux x86_64)

Date 2026-10-26, on `main` after e3d67b4, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock` (shared with the parallel RP-22 worker).

RP-19A (`tk-rackpy-19a-dsp-real-and-complex-fft-fir-convolut-ee16a5`) binds
the 35 records of `dsp/fft.hpp` and `dsp/fir.hpp`. Reference page:
[docs/reference/dsp.md](../docs/reference/dsp.md#ffts).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --no-test` | builds, including `rackpy_dsp_reference_sanitized` (ASan + UBSan) |
| `uv run pytest -q tests/coverage tests/dsp tests/docs` | 75 passed: the in-Rack DSP suite has 37 tests (7 RP-19A ones); `tests/dsp/test_advanced.py` runs the sanitized reference clean, leak detection on |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/dsp/in_rack_basic/test_advanced.py`,
production build):

- **Reference cases.** Real FFTs of 32, 96, 160 and 1024 points and complex
  FFTs of 16, 48, 80 and 512, each forward and inverse in both orders and
  scaled; direct convolutions of 0 to 64 taps; boxcar impulse responses at
  three cutoffs and four lengths; and real-time convolvers with blocks of 16,
  32 and 64 and kernels of 0, 1, 100 and 200 taps over eight blocks. Every
  case replays identically.
- **Behaviour.** Length rules (lengths with a factor 7, a non-multiple of 32,
  0, negative and above 65536 refused), normalization (`irfft(rfft(x)) =
  n x`), canonical order and a known bin, a flat complex spectrum, in-place
  transforms, buffer refusals (short, wrong type, misaligned, partly
  overlapping, read-only, strided), the `length` field's role, setup handles'
  identity and lifetime, refused copies, and the convolver's block sizes,
  identity kernel, in-place block, re-resolving views and read-only fields.

Only Linux x86_64 was tested.
