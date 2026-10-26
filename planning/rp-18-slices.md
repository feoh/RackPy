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
