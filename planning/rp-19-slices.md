# RP-19 bounded implementation slices

RP-19 (`tk-rackpy-19-bind-dsp-fft-resampling-fir-minblep-an-c8842c`) binds
Rack's FFTs, FIR helpers, MinBLEP, resamplers and ring buffers: 114 records
in five headers. It is split into bounded children, each closed with a
commit on `main`. Decisions follow the owner's standing instruction to take
the recommended option.

| Slice | Scope |
| --- | --- |
| 19A | `dsp/fft.hpp` and `dsp/fir.hpp` (35 records) |
| 19B | `dsp/minblep.hpp` and `dsp/resampler.hpp` (36) |
| 19C | `dsp/ringbuffer.hpp` (43), whose `RingBuffer<T, S>` needs a generic contract (coverage policy 10) |

**RP-19A — FFTs, FIR convolution and the real-time convolver**
(`tk-rackpy-19a-dsp-real-and-complex-fft-fir-convolut-ee16a5`). 35 records.
Validation: [rp-19a-validation.md](rp-19a-validation.md).

- **PFFFT's rules, checked first.** PFFFT checks its length rules with
  `assert` only (a no-op in release builds) and returns a null setup for
  lengths it cannot factor, which Rack's destructor dereferences; given no
  work buffer (Rack never passes one) it puts 4 or 8 bytes per point of
  scratch on the stack; and it needs 16-byte-aligned buffers. RackPy
  validates lengths (`2^a 3^b 5^c`, multiples of 32 real or 16 complex, at
  most 65536), sizes, item types, alignment and overlap before Rack sees
  them. Evidence: PFFFT's `pffft_new_setup` and `pffft_transform_internal`
  (upstream `pffft.c`).
- **Ownership.** The FFT keeps its true size beside Rack's writable `length`
  field; `setup` and `pffft` are opaque `PFFFT_Setup` handles that keep their
  owner alive; copies are refused (a C++ copy frees the setup twice).
  `RealTimeConvolver`'s owning arrays are views that re-resolve on every
  access, as `set_kernel` reallocates; `block_size` and `kernel_blocks` are
  read-only because Rack sizes its allocations by them.
- **Sanitizers.** `rackpy_dsp_reference_sanitized` runs the whole DSP
  reference under ASan and UBSan (`tests/dsp/test_advanced.py`); clean,
  leak detection included.
- **Rack errata.** Rack's comments give a real FFT's output as `2 * length`
  floats; PFFFT writes `length`. `minBlepImpulse` (19B) treats the real FFT's
  output the same way, reading past the `length` floats PFFFT wrote into its
  zeroed buffer.
