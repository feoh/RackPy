# RP-18 bounded implementation slices

RP-18 (`tk-rackpy-18-bind-dsp-common-digital-filter-and-mat-bba6ed`) binds
Rack's DSP helpers, triggers, filters, VU meters and MIDI processors: 303
records in nine headers. It is split into bounded children, each closed with
a commit on `main`. Decisions follow the owner's standing instruction to take
the recommended option.

**Ordering (2026-10-26).** Coverage policy 10 requires element-type templates
(`TSchmittTrigger<T>`, `TBiquadFilter<T>`, `dbToAmplitude<T>`, ...) to map at
least `float` and `simd::float_4`, so RP-20 (SIMD) went first and now blocks
RP-18 in Witan.

**Compile-time integer templates.** C++ instantiates one function or class per
value, so RackPy instantiates a bounded range and refuses values outside it,
recorded per record and listed as an owner review item in
[docs/reference/dsp.md](../docs/reference/dsp.md#templates):

- `polyDirect/Horner/Estrin<T, N>`: `N = 1` to `32` (18A).
- `IIRFilter<B_ORDER, A_ORDER, T>`: a bounded range of orders, chosen in 18C.
- `MidiGenerator<CHANNELS>`, `MidiParser<MAX_CHANNELS>`: 1 to 16 channels
  (18D), Rack's port and MIDI channel limit.
- `Frame<CHANNELS, T>` is plain data, so it is written in Python for any
  `CHANNELS` and needs no bound.

| Slice | Scope |
| --- | --- |
| 18A | `dsp/common.hpp`, `approx.hpp`, `convert.hpp`, `window.hpp`, `ode.hpp` (53 records) |
| 18B | `dsp/digital.hpp`: triggers, pulse generator, timers, clock divider (57) |
| 18C | `dsp/filter.hpp`: RC, exponential, peak, slew, IIR and biquad filters (78) |
| 18D | `dsp/vumeter.hpp` and `dsp/midi.hpp`: VU meters, MIDI generator and parser (115) |

**RP-18A — DSP scalar helpers, approximations, conversion, windows and ODE
steppers** (`tk-rackpy-18a-dsp-scalar-helpers-approximations-con-fd4045`). 53
records. Validation: [rp-18a-validation.md](rp-18a-validation.md).

- **Templates.** `float` and `float_4` overloads, the float one first, as in
  `rackpy.raw.simd`. `sinc` maps Rack's `sinc(float)` overload for floats and
  the template for vectors.
- **Polynomials.** One out-of-line instantiation per `(T, N)`
  (`native/bindings/dsp/poly.hpp`), shared with the C++ reference: the SDK's
  `-funsafe-math-optimizations` let GCC reassociate `polyDirect` and
  `polyEstrin` differently when inlined at two call sites (by several ULPs
  where terms cancel, and once `-inf` against NaN).
- **Undefined behaviour refused.** `exp2Floor<float>` (and `exp2_taylor5`)
  outside `-1 < x + 127 < 256`, where Rack's int conversion or shift is
  undefined; ODE `len` outside 1 to 4096 (stack arrays); overlapping
  `convert` buffers of different types (strict aliasing).
- **convert.** A template object keyed by `float`/`ctypes.c_float`,
  `ctypes.c_int8/16/32/64` and `Int24`; the eleven specializations take one
  sample or `(in_, out, len)` buffers. Rack's float-to-integer conversions
  clamp only above, so values below the minimum wrap; kept and documented.
- **ODE.** `f(t, x, dxdt)` is a Python callable, called synchronously on
  copies (read-only `x`, zeroed `dxdt`), so Rack's stack arrays never reach
  Python.
- **Frame.** Python (`src/rackpy/raw/_dsp.py`): `Frame[16]`,
  `Frame[4, float_4]`, buffer protocol in Rack's layout.

**RP-18B — DSP triggers, pulse generator, timers and clock divider**
(`tk-rackpy-18b-dsp-triggers-pulse-generator-timers-a-f72dae`). 57 records.
Validation: [rp-18b-validation.md](rp-18b-validation.md).

- **Objects.** Native classes holding Rack's structs: `BooleanTrigger`,
  `SchmittTrigger` (Rack's `TSchmittTrigger<float>` specialization),
  `PulseGenerator`, `Timer`, `ClockDivider`, with Rack's enums nested.
- **Templates.** `TSchmittTrigger` and `TTimer` are subscriptable;
  `[float]` and `[()]` give Rack's alias types, `[float_4]` the vector
  instantiations, which also have names (`TSchmittTrigger_float_4`,
  `TTimer_float_4`) so the manifest and stubs can refer to them. Vector
  fields are live views.
- **Deprecated union fields.** `state` (a `bool` in a union with `s`) warns,
  reads true unless `s` is `LOW` and writes through `s`, avoiding the
  undefined read of an invalid bool.
- **Out-of-line wrappers.** Not needed: these are comparisons, single
  additions and bit operations.

**RP-18C — DSP filters** (`tk-rackpy-18c-dsp-filters-rc-exponential-peak-slew--7c6985`).
78 records. Validation: [rp-18c-validation.md](rp-18c-validation.md).

- **Filters.** Native classes for `float` (Rack's aliases) and `float_4`
  (`T<Name>_float_4`), subscriptable templates, live fields and array views
  (`FloatArray`, `Float4Array`). Every method goes through one generic
  `[[gnu::noipa]]` member wrapper (`calls.hpp`), shared with the reference.
- **IIRFilter.** Orders 2 to 8 for both coefficient counts and both types
  (98 classes in `rackpy.raw.dsp._iir`, reached as `IIRFilter[B, A, T]`; the
  biquads' base `IIRFilter_3_3_float` and `_float_4` are also named). Below
  2, Rack's `process()` writes past a zero-length array. Owner review item.
- **Biquads.** One `BiquadFilter.Type` for both instantiations. A new
  biquad's coefficients are NaN (Rack's constructor computes 0 / 0).
- **Bulk path.** `rackpy.dsp.filters.process_block` runs the same wrapper
  per sample over float32 buffers: results equal a loop of `process` calls
  exactly, 20 to 27 times faster (`benchmarks/rp18-dsp-call-cost.json`,
  diagnostic).

**RP-18D — VU meters and the DSP MIDI generator and parser**
(`tk-rackpy-18d-vu-meters-and-the-dsp-midi-generator--3ba44c`). 115 records
(114 implemented, 1 planned). Validation: [rp-18d-validation.md](rp-18d-validation.md).

- **VU meters.** `VuMeter2` with its `Mode`; Rack's deprecated `VuMeter`
  warns on construction (its `dBScaled` starts at 0, where Rack leaves it
  uninitialized) and the deprecated `VUMeter` typedef warns through the
  module's `__getattr__`.
- **Templates.** `MidiGenerator[N]` and `MidiParser[N]` for N = 1 to 16
  (owner review item); the 16-channel classes are named, the others live in
  `rackpy.raw.dsp._midi`. One pair of parser mode enums for every
  instantiation.
- **onMessage.** An engine-strategy virtual (ADR 003): no Python override (a
  subclass defining `on_message` raises `TypeError`); RackPy's generator
  records the messages natively, and `take_messages()` returns them. The
  record stays `planned` for RP-22's laboratory (agreed with the RP-22 worker:
  its child RP-22E, blocked by RP-18D, adds the laboratory override).
- **Refusals.** Out-of-range channel and controller indices, more channels
  than the template's, MPE messages on channels at or above `MAX_CHANNELS`,
  a timing clock with `clockDivision` 0, and untrusted `from_json` input with
  out-of-range channels or enum values.
- **Views.** Array fields (`Int8Array`, `UInt8Array`, `Int16Array`,
  `BoolArray`, `ExponentialFilterArray`, `PulseGeneratorArray`, shared in
  `native/bindings/dsp/arrays.hpp` with RP-18C's) are live; object items and
  pulse fields are live views.
