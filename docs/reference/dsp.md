# DSP building blocks

Rack's `dsp` headers, family 6 of the [SDK surface](surface/dsp.md)
(**RP-18**, in slices). **Implemented, Linux x86_64 inside Rack Free 2.6.6:**
`dsp/common.hpp`, `approx.hpp`, `convert.hpp`, `window.hpp` and `ode.hpp`
(RP-18A), `dsp/digital.hpp` (RP-18B), `dsp/filter.hpp` (RP-18C), and
`dsp/vumeter.hpp` and `dsp/midi.hpp` (RP-18D), `dsp/fft.hpp` and
`dsp/fir.hpp` (RP-19A), `dsp/minblep.hpp` and `dsp/resampler.hpp`
(RP-19B), and `dsp/ringbuffer.hpp` (RP-19C), as `rackpy.raw.dsp` and the
{py:mod}`rackpy.dsp.basic`, {py:mod}`rackpy.dsp.digital`,
{py:mod}`rackpy.dsp.filters`, {py:mod}`rackpy.dsp.midi`,
{py:mod}`rackpy.dsp.fft`, {py:mod}`rackpy.dsp.resampling` and
{py:mod}`rackpy.dsp.ringbuffer` imports (see the
[coverage report](coverage.md)).

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
- `IIRFilter<B_ORDER, A_ORDER, T>` is instantiated for both orders from 2 to
  8 ([below](#iir-filters)). Below 2, Rack's `process()` writes past a
  zero-length array, so those orders are refused rather than offered.
- `MidiGenerator<CHANNELS>` and `MidiParser<MAX_CHANNELS>` are instantiated
  for 1 to 16 channels, Rack's limit for ports and MIDI channels
  ([below](#midi-generator)).
- `MinBlepGenerator<Z, O, T>` for `Z` and `O` = 4, 8, 16, 32, 64;
  `Decimator<OVERSAMPLE, QUALITY, T>` and `Upsampler<OVERSAMPLE, QUALITY>`
  for 2, 4, 8, 16, 32; `SampleRateConverter<MAX_CHANNELS>` for 1 to 16
  ([below](#minblep)).
- `RingBuffer<T, S>` and `DoubleRingBuffer<T, S>` for `S` = 16 to 32768 and
  `AppleRingBuffer<T, S, N>` for `S` = 16 to 4096 with `N` = 16 `S` (powers
  of 2), each for `T` = float, float_4, `Frame<2>`, `Frame<8>` and
  `Frame<16>`: the generic contract coverage policy 10 asks for
  ([below](#ring-buffers)).
- `Frame<CHANNELS, T>` is plain data, so it is written in Python and takes any
  `CHANNELS` from 1 ([below](#frames)).

**Approved by the owner (2026-10-26):** the sample-rate converter's 1 to 16
channels, the MinBLEP, decimator and upsampler sizes, and the ring buffers'
element types and sizes.

**Owner review item:** the `poly*` bound of 32, the `IIRFilter` orders 2 to
8 and the MIDI processors' 1 to 16 channels are RackPy's choices, recorded
in the manifest. A full-coverage release needs the owner to confirm them, or
to choose another contract ([release bar](../coverage.md#release-bar)).

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

(dsp-triggers)=
## Triggers

These are Rack's stateful objects. Each Python object holds Rack's C++ struct,
and its methods run Rack's code; like any Python value, use one from one
thread at a time. `copy.copy` copies the state; the objects are unhashable.

- `BooleanTrigger` detects a bool turning true. `process(in_)` returns
  whether it did; `process_event(in_)` returns a `BooleanTrigger.Event`:
  `TRIGGERED`, `UNTRIGGERED` (when it turns false) or `NONE`. `s` is the
  `BooleanTrigger.State` (`LOW`, `HIGH`, `UNINITIALIZED`), `is_high()` and
  `reset()` are Rack's. A new or reset trigger is `UNINITIALIZED`, so a first
  true input does not trigger.
- `SchmittTrigger` is Rack's `TSchmittTrigger<float>` specialization:
  `process(in_, low_threshold=0, high_threshold=1)` returns true when the
  input rises from `LOW` to at least the high threshold, and the state falls
  at or below the low one. `process_event` and `s` are as for
  `BooleanTrigger`, with its own `State` and `Event` enums. NaN reaches
  neither threshold, so it changes nothing.
- `TSchmittTrigger[float_4]` (also named `TSchmittTrigger_float_4`) is Rack's
  generic template for vectors: `process` returns a mask of the lanes that
  rose, and `state` is the state mask, a live view. Rack resets it to a full
  mask (every lane `HIGH`), unlike the float specialization.
- `TSchmittTrigger` is the template itself: `TSchmittTrigger[float]` and
  `TSchmittTrigger[()]` are `SchmittTrigger` (Rack's alias for
  `TSchmittTrigger<>`); other arguments raise `TypeError`.
- **Deprecated.** Rack's `bool state` member of both float triggers is a union
  with `s`. `state` emits `DeprecationWarning`; it reads true unless `s` is
  `LOW`, which is what the union's byte means (reading the C++ bool while `s`
  is `UNINITIALIZED` would be undefined), and assigning it sets `HIGH` or
  `LOW`.

(dsp-pulse)=
## Pulse generator

`PulseGenerator` holds `HIGH` for a duration: `trigger(duration=1e-3)`
starts a pulse unless the current one lasts longer, `process(delta_time)`
advances it and returns whether it was `HIGH` before the step, `is_high()`
reads it, `reset()` ends it, and `remaining` is the time left.

(dsp-timers)=
## Timers and the clock divider

- `Timer` (`TTimer<float>`) accumulates `process(delta_time)` and returns the
  time since `reset()`; `get_time()` and `time` read it. `TTimer[float_4]`
  (`TTimer_float_4`) keeps four, with `time` a live view. `TTimer` is the
  template, as for `TSchmittTrigger`.
- `ClockDivider.process()` counts calls and returns true, resetting the count,
  every `division` calls (`set_division`, `get_division`, `get_clock`,
  `reset`, and the `clock` and `division` fields). Both are `uint32_t`: values
  outside raise `OverflowError`, and the count wraps as unsigned arithmetic
  does. A division of 0 or 1 returns true every call.

(dsp-filters)=
## Filters

Rack's filters are stateful objects like the [triggers](#triggers), each
instantiated for `float`, named by Rack's alias, and for `float_4`, named
`T<Name>_float_4`: `RCFilter` and `TRCFilter_float_4`. The templates
themselves are subscriptable, as `TRCFilter[float_4]`. Vector fields are live
views, and every method calls Rack's code out of line
(`native/bindings/dsp/calls.hpp`).

- `RCFilter`: an RC circuit by Euler's method. `set_cutoff(r)` takes the
  angular cutoff in radians (`c = 2 / r`), `set_cutoff_freq(f)` the cutoff as a
  ratio of the sample rate; `process(x)` steps it, and `lowpass()` and
  `highpass()` read the outputs. `xstate` and `ystate` are Rack's one-element
  arrays, as live views.
- `ExponentialFilter`: exponential smoothing. `set_lambda`, `set_tau`
  (`lambda = 1 / tau`) and `process(delta_time, in_)`, which returns the new
  output and snaps to the input once a step no longer changes the output.
  Rack's field `lambda` is `lambda_`, as `lambda` is a Python keyword.
- `PeakFilter`: jumps to higher values at once and decays toward lower ones.
- `SlewLimiter`: `set_rise_fall(rise, fall)` in units per second;
  `process(delta_time, in_)` moves toward the input by at most that much.
- `ExponentialSlewLimiter`: exponential smoothing with separate rise and fall
  rates, `set_rise_fall` and `set_rise_fall_tau`.
- **Deprecated.** `process(in_)` with one argument (Rack's `process(1, in)`)
  on the four smoothers, and `PeakFilter.peak()` and `set_rate(r)`
  (`lambda = 1 - r`), emit `DeprecationWarning`.

(dsp-iir)=
## IIR filters

`IIRFilter[b_order, a_order]` (or `[b_order, a_order, float_4]`) is Rack's
`IIRFilter<B_ORDER, A_ORDER, T>`, with `B_ORDER` numerator coefficients and
`A_ORDER` denominator ones, `a_0 = 1` omitted:

- `b`, `a`, `x` and `y` are Rack's coefficient and state arrays, as live
  views (`b[0]` of a vector filter is itself a live `float_4`).
- `set_coefficients(b, a)` takes `B_ORDER` b and `A_ORDER - 1` a coefficients
  (Rack reads exactly that many from two arrays), `reset()` zeroes the state
  but not the coefficients, and `process(in_)` returns the next output.
- `get_transfer_function(s)` is `H` at the normalized angular frequency
  `s = 2πf/f_s`: a Python complex, or a `(real, imag)` pair of `float_4` for
  vectors. `get_frequency_response(f)` and `get_frequency_phase(f)` take
  `f = f_c/f_s`.

Each instantiation is a class (`IIRFilter[3, 3]` is `IIRFilter_3_3_float`,
the biquads' base); a class's `B_ORDER` and `A_ORDER` say which.

(dsp-biquad)=
## Biquad filters

`BiquadFilter` (and `TBiquadFilter[float_4]`) is an `IIRFilter[3, 3]` with
Rack's designs: `set_parameters(type, f, q, v)` sets the coefficients for a
`BiquadFilter.Type` (`LOWPASS_1POLE`, `HIGHPASS_1POLE`, `LOWPASS`,
`HIGHPASS`, `LOWSHELF`, `HIGHSHELF`, `BANDPASS`, `PEAK`, `NOTCH`), at
`f = f_c/f_s` (below 0.5), quality factor `q` and gain `v`. `NUM_TYPES` sets
nothing. Both instantiations take `BiquadFilter.Type`; in C++ each has its own
`Type` with the same values.

**A new biquad's coefficients are NaN.** Rack's constructor calls
`setParameters(LOWPASS, 0, 0, 1)`, which computes `K / Q = 0 / 0`, so a
`BiquadFilter` outputs NaN until `set_parameters` gives it real values.

(dsp-bulk)=
## Whole buffers

`rackpy.dsp.filters.process_block(filter, in_, out, delta_time=None)` runs
a float filter over a float32 buffer natively, writing `out` (which may be
`in_`). It is RackPy's convenience, not Rack API: each sample goes through
the same Rack call as `filter.process`, in order, so the outputs and the
filter's final state are exactly those of a loop of `process` calls. The
exponential, peak and slew filters need `delta_time`; the IIR and biquad
filters take none; RC filters have no single output and are refused. The
buffers are read and written in place, with no copies. [What it
costs](#what-the-calls-cost) compares it with a Python loop.

(dsp-cost)=
## What the calls cost

Measured inside Rack Free 2.6.6 on the planning machine (an i9-14900K desktop
under load, with the `powersave` governor: diagnostic numbers, not a
performance claim; raw data in
[benchmarks/rp18-dsp-call-cost.json](../../benchmarks/rp18-dsp-call-cost.json)),
median nanoseconds per sample over 20,000 samples:

| Workload | ns per sample |
| --- | --- |
| `BiquadFilter.process`, a Python loop of calls | 72 |
| `BiquadFilter`, `process_block` | 2.7 |
| `SlewLimiter.process(delta_time, x)`, a Python loop | 67 |
| `SlewLimiter`, `process_block` | 3.3 |
| `SchmittTrigger.process`, a Python loop | 71 |
| `cubic(x)`, a Python loop | 32 |
| `TBiquadFilter[float_4].process`, a loop (4 samples per call) | 45 |

A Python call costs tens of nanoseconds whatever it computes, so
`process_block` is 20 to 27 times faster for these filters; it copies
nothing, reading and writing the caller's buffers. Neither is a way to run
audio: at 48 kHz Rack calls a module's `process()` every 21 microseconds, on
its engine thread, where Python never runs ([process audio](../how-to/dsp.md)).

(dsp-vumeter)=
## VU meters

`VuMeter2` smooths a level for meter lights: `process(delta_time, value)`
tracks the peak (`Mode.PEAK`, jumping up and decaying) or the mean square
(`Mode.RMS`) at the inverse time constant `lambda_` (30 per second), in `v`;
`get_brightness(db_min, db_max)` is 0 at `db_min` and below, 1 at `db_max`
and above, and linear in dB between; `reset()` zeroes it. Rack suggests
calling `get_brightness` every few hundred frames, not every frame.

`VuMeter` is Rack's deprecated meter (`set_value(v)`, then
`get_brightness(i)` for light `i`, with `db_interval` decibels between
lights), and `VUMeter` its deprecated typedef: both emit `DeprecationWarning`.
Its `db_scaled` starts at 0, where Rack leaves it uninitialized until
`set_value`.

(dsp-midi-generator)=
## MIDI generator

`MidiGenerator[channels]` (`MidiGenerator[16]` is `MidiGenerator_16`) turns
gates and CV into MIDI messages, as Rack's CV to MIDI does:
`set_note_gate(note, gate, c)` after `set_velocity(vel, c)`,
`set_key_pressure`, `set_channel_pressure`, `set_cc(cc, id)` and its
shortcuts (`set_mod_wheel`, `set_volume`, `set_balance`, `set_pan`,
`set_sustain_pedal`), `set_pitch_wheel`, the transport (`set_clock`,
`set_start`, `set_continue`, `set_stop`), `set_frame`, `set_channels`,
`reset` and `panic`. The setters send a message only when their value
changes; `panic()` resets and sends a note off for every note. The
fields (`notes`, `gates`, `ccs`, `pw`, ...) are Rack's, the arrays as live
views.

Rack hands each message to the virtual `onMessage`, which a C++ subclass
overrides. It is an engine-thread callback (ADR 003), so Python cannot
override it: RackPy's generator implements it by keeping the messages, and
`take_messages()` returns those generated since the last call, as
`rackpy.raw.midi.Message` objects, oldest first. A subclass that defines
`on_message` raises `TypeError`, as it would never be called. A Python
override for non-real-time use is RP-22's laboratory.

Indices are checked: a channel `c` must be below `CHANNELS`, a controller
`id` below 128, and `set_channels` at most `CHANNELS`, where Rack would
index past its arrays. Values must fit their C++ types (`OverflowError`).

(dsp-midi-parser)=
## MIDI parser

`MidiParser[channels]` (`MidiParser_16`, as Rack's MIDI to CV uses) turns
MIDI messages into gates, CV and pulses: `process_message(msg)` (or
`process_cc`, `process_system`), then `process_filters(delta_time)` and
`process_pulses(delta_time)` every frame, and `get_pitch_voltage(c)`,
`get_pw(c)`, `get_mod(c)`, `gates`, `velocities`, `aftertouches` and the
pulses (`clock_pulse`, `retrigger_pulses`, ...) to read the result. The modes
are `MonoMode` (last, first, lowest or highest priority) and `PolyMode`
(rotate, reuse, reset, MPE), one pair of enums shared by every
instantiation. `to_json()` and `from_json(json)` save and restore the
settings with Rack's keys, as Python values.

Rack trusts its inputs here; RackPy refuses what it would get wrong:

- channel indices of `MAX_CHANNELS` or more (`IndexError`), and more channels
  than `MAX_CHANNELS` (`ValueError`);
- in polyphonic MPE mode, Rack indexes its arrays by a message's MIDI
  channel, so with fewer than 16 channels a message on a higher channel
  raises `ValueError`;
- a timing clock message while `clock_division` is 0, which Rack would
  divide by (`clock_division` itself may be 0);
- in `from_json`, a `channels` above `MAX_CHANNELS`, or a `monoMode` or
  `polyMode` outside its enum, which would make Rack index past its arrays or
  build an invalid enum value. Patch JSON is untrusted.

(dsp-fft)=
## FFTs

`RealFFT(length)` and `ComplexFFT(length)` are Rack's wrappers of PFFFT.
Each owns a PFFFT setup (`setup`, an opaque handle with identity, read-only
because the FFT frees it; it keeps its FFT alive), and a copy is refused, as
copying the C++ object would free the setup twice.

- **Lengths.** PFFFT takes `2^a 3^b 5^c` points, a multiple of 32 for a real
  FFT and of 16 for a complex one. It checks most of that only with `assert`,
  which a release build skips, and it returns no setup for other lengths,
  which Rack's destructor would then dereference: RackPy raises `ValueError`
  first. Rack gives PFFFT no work buffer, so PFFFT puts its scratch (4 bytes
  per point real, 8 complex) on the stack; RackPy allows lengths up to 65536.
- **Buffers.** Contiguous float32 buffers, 16-byte aligned (PFFFT's SSE code
  needs it; an `array.array` or numpy array is aligned, a slice of one may
  not be), of the transform's size: `length` floats in and out for a real
  FFT, `2 * length` (interleaved complex numbers) for a complex one. The
  input and output may be the same buffer, but not partly overlap. Rack's
  comments say a real FFT's output is `2 * length` floats; PFFFT writes
  `length`.
- **Order.** `rfft`/`fft` give the canonical order: for a real FFT `F(0)`,
  `F(n/2)`, then the real and imaginary parts of `F(1)` to `F(n/2 - 1)`;
  `rfft_unordered`/`fft_unordered` give PFFFT's internal order, faster and
  consistent between transforms, for convolution or inverting.
- **Scale.** Nothing is normalized: the inverse of a forward transform is
  `length` times the input. `scale(x)` multiplies `length` floats (`2 *
  length` complex) by `1 / length`. It reads the writable `length` field;
  the transforms always use the setup's size.

(dsp-fir)=
## FIR helpers and the real-time convolver

- `convolve_naive(in_, kernel, len)` is one output of a direct convolution,
  `sum(in_[len - 1 - i] * kernel[i])`; `boxcar_lowpass_ir(out, len,
  cutoff=0.5)` writes `len` taps of an unwindowed sinc lowpass at `cutoff`
  (a ratio of the sample rate). Buffers are float32 with at least `len`
  items; alignment does not matter.
- `RealTimeConvolver(block_size)` convolves a block at a time with a long
  impulse response, by partitioned FFTs. `2 * block_size` must be a PFFFT
  real length (16, 32, 48, 64, ...; Rack recommends a power of 2 from 32),
  and `block_size` at most 32768. `set_kernel(kernel, length)` takes the impulse response
  (`None` or a length of 0 clears it, up to 4,194,304 samples) and allocates
  its FFTs; `process_block(input, output)` convolves `block_size` floats,
  with no latency, and the output may be the input.
- Its arrays (`kernel_ffts`, `input_ffts`, `output_tail`, `tmp_block`) are
  views that find the array again at every access, as `set_kernel`
  reallocates them, so a view never dangles. `block_size`, `kernel_blocks`
  and the `pffft` handle are read-only: Rack sizes the arrays by them, and
  changing them in C++ would make it read and write past its allocations.
  `input_pos` is writable; Rack reduces it modulo `kernel_blocks`.
- RackPy's tests also run every case of the reference under AddressSanitizer
  and UndefinedBehaviorSanitizer (`tests/dsp/test_advanced.py`), with Rack's
  inline code instrumented.

(dsp-minblep)=
## MinBLEP

`min_blep_impulse(z, o, output)` writes Rack's minimum-phase band-limited
step, `2 z o` floats rising from 0 to 1. Rack computes it with a `RealFFT` of
`2 z o` points, so that must be a PFFFT real length of at most 65536
(`ValueError` otherwise; Rack would crash).

`MinBlepGenerator[z, o]` (or `[z, o, float_4]`; `MinBlepGenerator[16, 32]`
is named `MinBlepGenerator_16_32_float`) corrects a naive oscillator's
discontinuities: `insert_discontinuity(p, x)` adds a step of `x` at
`-1 < p <= 0` frames relative to the current frame (other `p` are ignored,
as in Rack), and `process()` returns the next correction to add. `buf`
(the pending output) and `impulse` are live views; `pos` must stay below
`2 z`, as Rack indexes `buf` with it.

(dsp-resampling)=
## Resampling

- `SampleRateConverter[channels]` (the 16-channel one is also named
  `SampleRateConverter_16`) converts interleaved audio between two rates with speexdsp:
  `set_rates(in_rate, out_rate)`, `set_quality(0 to 10)` and
  `set_channels(0 to MAX_CHANNELS)`, each rebuilding speexdsp's state (`st`,
  an opaque handle, false when the rates are equal and Rack only copies).
  `process(in_, in_stride, in_frames, out, out_stride, out_frames)` reads up
  to `in_frames` frames and writes up to `out_frames`, with channel `c` of
  frame `i` at `i * stride + c`, and returns `(frames read, frames written)`,
  which C++ returns through two `int` pointers. Buffers are float32 and must
  hold the frames (`ValueError`); strides must be at least `channels`.
  `process(frames, in_frames, out_frames_list, out_frames)` takes lists of
  `Frame[MAX_CHANNELS]` objects instead, copied into and out of Rack's
  contiguous frames.
- **Fields.** `channels` is read-only: speexdsp indexes its state by channel
  without bounds checks, so changing it without rebuilding the state would
  make Rack read and write past its allocation, and `set_channels` above
  `MAX_CHANNELS` is refused where Rack only asserts. `quality`, `in_rate` and
  `out_rate` can be assigned, as in C++, without rebuilding the state
  (`refresh_state()` does); rates must be 1 to 3,072,000.
- `Decimator[oversample, quality]` (or `[..., float_4]`) downsamples by
  `oversample` with an `oversample * quality`-tap windowed-sinc filter:
  `process(in_)` takes `oversample` inputs (a float32 buffer, or a list of
  float_4) and returns one output. `Upsampler[oversample, quality]` upsamples:
  `process(in_, out)` writes `oversample` outputs for one input. Both take a
  `cutoff`, as a ratio of the lower rate's Nyquist frequency (0.9 by default), and keep
  `in_index` within the range Rack's code keeps it in, as it copies or
  indexes from there.

(dsp-ring-buffers)=
## Ring buffers

Rack's ring buffers are templates over an element type and compile-time
sizes, so no binding can take every `T` and `S`. RackPy's generic contract
(coverage policy 10; approved by the owner on 2026-10-26) instantiates:

- `T`: float, `float_4`, and `Frame[2]`, `Frame[8]` and `Frame[16]`, the
  frame sizes of Rack's audio interfaces.
- `RingBuffer[T, S]` and `DoubleRingBuffer[T, S]`: `S` = 16, 32, ..., 32768.
- `AppleRingBuffer[T, S, N]`: `S` = 16, 32, ..., 4096 and `N` = 16 `S`, the
  ratio of Rack's own example (64 in 1024).

Subscripting with anything else raises `TypeError` and lists the set.
`RingBuffer[float, 1024]`, `DoubleRingBuffer[float, 1024]` and
`AppleRingBuffer[float, 64, 1024]` are also named
(`RingBuffer_float_1024`, ...).

Elements cross as Python values: `push(t)` takes a float, a `float_4` or a
`Frame[C]`, and `shift()` returns a new one. Bulk copies (`push_buffer`,
`shift_buffer`) take float32 buffers holding the elements' lanes (one float
per float element, four per `float_4`, `C` per `Frame[C]`). `data` and the
pointers Rack returns are live views of the inline data, which never moves,
so a view stays valid, if stale, as long as it keeps the buffer alive. The
`const T*` results (`start_data`) are read-only views. `Frame` elements
appear in views as their floats. New buffers are empty with zeroed data
(C++'s default initialization leaves the data indeterminate).

- **`RingBuffer`**, Rack's single-producer, single-consumer lock-free queue.
  Its `start` and `end` only grow (wrapping at 2^64) and are taken modulo `S`.
  As in Rack, `push` to a full buffer overwrites the oldest element and
  `shift` from an empty one returns a stale element; `size()` and
  `capacity()` then wrap. `push_buffer` and `shift_buffer` refuse `n` above
  `S`, which would copy past the data. C++ cannot copy it (its indices are
  atomics), so neither can Python.
- **`DoubleRingBuffer`** keeps a copy after the data (`data` has `2 S`
  elements), so `end_data()` gives `S` writable elements to append (then
  `end_incr(n)`, `n` at most `S`) and `start_data()` `S` to read (then
  `start_incr(n)`). Otherwise it behaves as `RingBuffer`. Not thread-safe.
- **`AppleRingBuffer`** slides a window along a block of `N` and moves it
  back with `return_buffer()`. Its indices are used unreduced, so RackPy keeps
  `0 <= start <= end <= N`: it refuses field writes outside that, `push` when
  the block is full, `shift` when empty, `end_data(n)` above `N - size()`,
  `end_incr` past `N` and `start_incr` past `end`. As in Rack, up to `N`
  elements may be held, past `S`; `full()` (`size() == S`) and `capacity()`
  follow its arithmetic. `start_data()` views the `size()` elements held.
  It copies as plain data.

The buffers hold the GIL during every call; `RingBuffer`'s lock-free
guarantee is between one producer and one consumer.
