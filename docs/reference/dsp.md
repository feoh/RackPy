# DSP building blocks

Rack's `dsp` headers, family 6 of the [SDK surface](surface/dsp.md)
(**RP-18**, in slices). **Implemented, Linux x86_64 inside Rack Free 2.6.6
(RP-18A):** `dsp/common.hpp`, `approx.hpp`, `convert.hpp`, `window.hpp` and
`ode.hpp`, as `rackpy.raw.dsp` and the {py:mod}`rackpy.dsp.basic` import (see
the [coverage report](coverage.md)). Triggers and timers (RP-18B), filters
(RP-18C), VU meters and the MIDI processors (RP-18D), and FFT, resampling,
FIR, MinBLEP and ring buffers (RP-19) are not bound yet.

Every function runs **Rack's own C++ code**, compiled with the SDK's flags.
`tests/dsp` replays more than 4,000 cases from a compiled C++ reference
(`native/tests/dsp_reference.cpp`) through these bindings, inside Rack, and
requires identical results, with the sign of a zero and NaN payloads ignored,
as for the [values](values.md#numbers-cross-as-c-numbers). As for
[SIMD](simd.md), the reference and the bindings call Rack's functions through
the same out-of-line wrappers (`native/bindings/dsp/calls.hpp`), because the
SDK's `-funsafe-math-optimizations` let the compiler round Rack's inline code
differently at each call site.

:::{admonition} Design and analysis, not audio
:class: note

A Python call costs far more than the sample it computes. Use these to design
and test DSP, to fill tables and to analyse signals; per-sample audio belongs
in a native kernel ([process audio](../how-to/dsp.md)), where C++ uses
`rack::dsp` directly.
:::

(dsp-templates)=
## Templates

Most of these are C++ templates over an element type `T`. RackPy instantiates
them for **`T = float` and `T = simd::float_4`**, Rack's two sample types: a
Python float picks the `float` instantiation, a
[`float_4`](simd.md#vectors-float_4-and-int32_4) the vector one, and a
scalar mixed with a vector broadcasts, as in `rackpy.raw.simd`.

Templates over a **compile-time integer** cannot follow Python's values
freely, because C++ instantiates one function per value. RackPy's contract:

- `poly_direct`, `poly_horner` and `poly_estrin` (`<T, N>`, with `N` the
  number of coefficients) are instantiated for `N = 1` to `32`. More
  coefficients raise `ValueError`; none raises too, as C++ has no
  zero-length arrays.
- `Frame<CHANNELS, T>` is plain data, so it is written in Python and takes any
  `CHANNELS` from 1 ([below](#frames)).

**Owner review item:** the `poly*` bound of 32 is RackPy's choice, recorded
in the manifest. A full-coverage release needs the owner to confirm it, or to
choose another contract ([release bar](../coverage.md#release-bar)).

(dsp-common)=
## Constants, conversions and scalings

- `FREQ_C4` (261.6256 Hz), `FREQ_A4` (440 Hz) and `FREQ_SEMITONE` (the ratio
  of a semitone), as C++ floats.
- `sinc(x)`: the normalized sinc, `sin(πx)/(πx)` with `sinc(0) = 1`. For a
  float, C++ calls its `sinc(float)` overload; for a `float_4`, the template.
- `amplitude_to_db(amp)` is `20 log10(amp)` and `db_to_amplitude(db)` is
  `10**(db/20)`.
- Parameter scalings: `quadratic_bipolar(x)` (`sgn(x) x²`), `cubic`,
  `quartic_bipolar`, `quintic`, `sqrt_bipolar(x)` (`sgn(x) √x`; Rack takes
  the root of the signed value, so it is NaN for `x < 0`) and
  `exponential_bipolar(b, x)` (`(bˣ - b⁻ˣ)/(b - 1/b)`, which Rack calls slow).

(dsp-approx)=
## Polynomials and fast powers of two

- `poly_direct(a, x)`, `poly_horner(a, x)` and `poly_estrin(a, x)` evaluate
  `Σ a[n] xⁿ`, directly, by Horner's method and by Estrin's scheme. `a` is a
  sequence of coefficients (floats, or float_4s for the vector form; floats
  broadcast). The three can round differently from each other, and
  `poly_direct` and `poly_estrin` also from the same call in your own C++,
  because the compiler may reassociate their sums at each call site: in
  RackPy's tests, by several ULPs where terms cancel, and once `-inf` against
  NaN.
- `exp2_floor(x)` returns `(2**floor(x), the fraction of x)`; C++ returns the
  fraction through a pointer. `exp2_taylor5(x)` is `2**x` with at most `6e-6`
  relative error. `approx_exp2_floor` and `approx_exp2_taylor5` are Rack's
  deprecated aliases and emit `DeprecationWarning`.
- **Refused inputs.** For a float, Rack's `exp2Floor` converts `x + 127` to
  an `int32_t` and shifts it left by 23 bits, which is undefined unless the
  sum (in float) is above -1 and below 256. RackPy raises `ValueError` outside
  that, for `exp2_floor`, `exp2_taylor5` and their aliases. The `float_4`
  forms use SSE conversions and shifts, which are defined for every lane.
  Rack's comment that the conversion "always truncates towards -inf" holds
  only for `x >= -127`.

(dsp-convert)=
## Sample conversion

`Int24` is Rack's packed 3-byte integer: `Int24(i)` keeps the low 24 bits of
an `int32_t`, sign-extended, as GCC stores Rack's bit-field (`Int24(2**23).i`
is `-2**23`). `i` is the field, and `int(x)` is its `int32_t` conversion.
`Int24()` is 0; Rack leaves it uninitialized.

`convert` is Rack's function template `convert<To, From>`, specialized by
subscripting with Python's names for the C types: `float` (or
`ctypes.c_float`), `ctypes.c_int8`, `c_int16`, `c_int32`, `c_int64` and
`Int24`. Rack deletes the primary template and defines eleven
specializations, float to and from each integer type and float to float;
any other pair raises `TypeError`.

```text
convert[float, ctypes.c_int16](16384)                 # 0.5
convert[ctypes.c_int16, float](0.5)                   # 16384
convert[ctypes.c_int16, float](src, out, len)         # buffers
```

- One sample: integers take a Python int in their range (`OverflowError`
  otherwise), floats a float.
- Buffers: `convert[To, From](in_, out, len)` converts `len` samples, where
  `in_` and `out` are contiguous buffers of the matching item types (an
  `Int24` buffer is single bytes, 3 per sample) and `out` is writable. A short
  buffer raises `ValueError`. Rack's loop reads and writes through two pointer
  types, so overlapping buffers of different types raise `ValueError` (that
  would be undefined); the same type may overlap.
- **Rack's float-to-integer conversions clamp only above.** They round with
  `llround` and limit the result to the type's maximum, but a value below the
  type's minimum is then narrowed by C++, which wraps: `convert[c_int8,
  float](-1000.0)` is 0, not -128. NaN gives the narrowing of `llround`'s
  unspecified result. RackPy keeps Rack's results.

(dsp-window)=
## Window functions

`hann(p)`, `blackman(alpha, p)` (Rack suggests `alpha = 0.16`),
`blackman_nuttall(p)` and `blackman_harris(p)` are the windows at
`p ∈ [0, 1]`. `hann_window(x, len)`, `blackman_window(alpha, x, len)`,
`blackman_nuttall_window(x, len)` and `blackman_harris_window(x, len)`
multiply the first `len` items of a writable float32 buffer by the window, in
place, at `p = i / (len - 1)`. A `len` larger than the buffer raises
`ValueError`; as in Rack, `len = 1` divides by zero and multiplies by NaN.

(dsp-ode)=
## ODE steps

`step_euler`, `step_rk2` and `step_rk4` advance `dx/dt = f(t, x)` by one step
of `dt`, updating `x` in place, with Rack's code:

```text
step_rk4(t, dt, x, len, f)        # f(t, x, dxdt) fills dxdt
```

- For `T = float`, `x` is a writable float32 buffer with at least `len`
  values; `t` and `dt` are floats. For `T = float_4`, `x` is a list of
  `float_4`, updated in place (the same vector objects change), and `t` and
  `dt` broadcast.
- `f` is called synchronously, 1, 2 or 4 times per step. It gets **copies**:
  `x` as a read-only `memoryview` (a `tuple` of vectors for `float_4`) and
  `dxdt` as a zeroed `array('f')` (a `list` of zero vectors) for it to fill;
  RackPy copies `dxdt` back into Rack's arrays when `f` returns. Rack's stack
  arrays therefore never reach Python, and what `f` keeps stays valid. Rack
  leaves `dxdt` uninitialized; RackPy zeroes it.
- An exception from `f` propagates to the caller. The steppers update `x`
  only after their last call to `f`, so `x` is unchanged then.
- `len` must be 1 to 4096: Rack's steppers put `len` values per stage on the
  stack, so a `len` below 1 is undefined and a large one would overflow the
  stack.

(dsp-frame)=
## Frames

`Frame` is Rack's `Frame<CHANNELS, T>`, `T samples[CHANNELS]`, which Rack's
ring buffers and sample-rate converter use for interleaved samples.
`Frame[16]` is `Frame<16>` (`T = float`), and `Frame[4, float_4]` is
`Frame<4, simd::float_4>`. A specialization is a cached class; instantiate it
to get a frame whose samples start at zero (C++ leaves them uninitialized).

- `frame.samples` is a live view: a writable float32 `memoryview` for
  `float`, and for `float_4` a view whose items are vectors, read out and
  written in.
- `memoryview(frame)` is the frame's float32 storage in Rack's layout
  (`CHANNELS × 4` floats for `float_4`, lane by lane).
- `Frame` itself is not instantiable; `CHANNELS` must be at least 1 and `T`
  must be `float` or `float_4`. A specialization's `CHANNELS` and `type` are
  its template arguments.
