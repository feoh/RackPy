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

**RP-19B — MinBLEP, the sample-rate converter, decimator and upsampler**
(`tk-rackpy-19b-minblep-and-the-sample-rate-converter-67a664`). 36 records.
Validation: [rp-19b-validation.md](rp-19b-validation.md).

- **Bounded contracts (owner review item).** `MinBlepGenerator<Z, O, T>` for
  Z and O = 4 to 64, `Decimator<OVERSAMPLE, QUALITY, T>` and
  `Upsampler<OVERSAMPLE, QUALITY>` for 2 to 32 (powers of 2), T = float and
  float_4; `SampleRateConverter<MAX_CHANNELS>` for 1 to 16, Rack's own
  modules using 16. Each is reached by subscripting.
- **Refusals.** `minBlepImpulse` builds a `RealFFT(2 z o)`, so 2 z o must be
  a PFFFT real length of at most 65536. `pos` and `in_index` stay within the
  range Rack indexes and copies with. `SampleRateConverter.channels` is
  read-only (speexdsp indexes its state by channel unchecked) and
  `set_channels` above MAX_CHANNELS is refused where Rack only asserts.
- **Out-of-line constructors.** The kernels are computed in inline
  constructors, so both the reference and the bindings construct through
  `calls.hpp`'s `make<X>()`.

**RP-19C — ring buffers**
(`tk-rackpy-19c-dsp-ring-buffers-ringbuffer-doublerin-9d17ea`). 43 records.
Validation: [rp-19c-validation.md](rp-19c-validation.md).

- **Generic contract (coverage policy 10; owner review item).** T = float,
  float_4, and `Frame<2>`, `Frame<8>`, `Frame<16>` (Rack's audio interfaces'
  frame sizes); `RingBuffer` and `DoubleRingBuffer` for S = 16 to 32768 and
  `AppleRingBuffer` for S = 16 to 4096 with N = 16 S (Rack's 64-in-1024
  example), powers of 2: 165 instantiations. Arbitrary `T` cannot cross to
  Python, and every `S` cannot be compiled.
- **Rack's semantics kept where defined.** `RingBuffer` and
  `DoubleRingBuffer` reduce their indices modulo S, so overruns, underruns
  and wrapped `size()` are memory-safe and follow Rack. Refused: bulk copies
  past S, and `AppleRingBuffer`'s unreduced indices leaving
  `0 <= start <= end <= N`.
- **Views.** Rack's pointers are live views of the inline data, which never
  moves; `const T*` results are read-only views (`ArrayView` gained a
  read-only mode). `Frame` elements cross as `Frame[C]` objects and appear in
  views as floats; `rackpy.raw._dsp` re-keys the Frame instantiations by
  `Frame[C]` once it has written `Frame`.
