# RP-19B validation (Linux x86_64)

Date 2026-10-26, on `main` after 99e3f5e, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock` (shared with the parallel RP-22 worker).

RP-19B (`tk-rackpy-19b-minblep-and-the-sample-rate-converter-67a664`) binds
the 36 records of `dsp/minblep.hpp` and `dsp/resampler.hpp`. Reference pages:
[MinBLEP](../docs/reference/dsp.md#minblep) and
[resampling](../docs/reference/dsp.md#resampling).

| Command | Result |
| --- | --- |
| `cmake --build build/dev` | builds, including `rackpy_dsp_reference_sanitized` (ASan + UBSan) |
| `uv run pytest -q tests/coverage tests/dsp tests/docs` | 75 passed; the in-Rack DSP suite now asserts that every `in_rack_basic/test_*.py` file reported (it includes the 5 RP-19B tests); `tests/dsp/test_advanced.py` runs the sanitized reference (4,333 cases) clean, leak detection on |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |
| `uv run bin/check_coverage.py check --without-built-package` | implemented 2869, planned 11, unreviewed 65 (the RP-19C ring buffers among them) |

**Inside actual Rack** (`tests/dsp/in_rack_basic/test_resampling.py`,
production build):

- **Reference cases.** Rack's minimum-phase impulse for (z, o) = (4, 4),
  (16, 32) and (8, 16); MinBLEP generators <16, 32, float> and
  <8, 8, float_4> over scripts of discontinuities, including ignored `p`
  outside (-1, 0] and NaN; speexdsp converters of 1, 2 and 16 channels
  (3 used), upsampling and downsampling at qualities 0, 4 and 10 and equal
  rates (Rack's copy path), over three chunks each; decimators <4, 8, float>
  and <8, 16, float_4> with their kernels; upsamplers <4, 8> and <8, 4> at
  cutoff 0.75. Every case replays identically.
- **Behaviour.** The bounded template sets and their refusals; the impulse's
  PFFFT length rule and buffer size; ignored discontinuities; `pos`,
  `in_index` range refusals (Rack indexes and copies from them); read-only
  `channels` and the state handle (false at equal rates); channel, quality
  and rate refusals; stride and buffer-size refusals; the `Frame` overload
  giving the buffer overload's results; wrong `Frame` sizes refused.

**Owner review item:** the MinBLEP (4 to 64), decimator and upsampler (2 to
32, powers of 2) and converter (1 to 16 channels) sets are RackPy's bounded
contracts, recorded in the manifest.

Only Linux x86_64 was tested.
