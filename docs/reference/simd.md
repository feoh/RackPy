# SIMD: four lanes at a time

Rack's `simd` headers (`simd/Vector.hpp`, `simd/functions.hpp`, and the
`sse_mathfun` approximations behind them), part of family 6 of the
[SDK surface](surface/dsp.md) (**RP-20**). **Implemented, Linux x86_64 inside
Rack Free 2.6.6:** `rackpy.raw.simd`, `rackpy.raw.sse_mathfun`, the SIMD
methods of `rackpy.raw.engine.Port`, and the {py:mod}`rackpy.simd` import
for everyday use (see the [coverage report](coverage.md)).

Every operator and function here runs **Rack's own C++ code**, compiled with
the SDK's flags. `tests/simd` replays more than 20,000 cases from a compiled
C++ reference (`native/tests/simd_reference.cpp`) through these bindings,
inside Rack, and requires every lane to match: bit for bit for bitwise logic,
masks and casts, and with the sign of a zero and NaN payloads ignored for
arithmetic, as for the [values](values.md#numbers-cross-as-c-numbers).

:::{admonition} What this is for
:class: note

A Python call costs far more than the four lanes it computes. Use these types
to explore and test Rack's SIMD semantics, to compute tables, and to set up
DSP; per-sample audio belongs in a native kernel
([process audio](../how-to/dsp.md)), where a C++ kernel uses `rack::simd`
directly.
:::

```{rackpy-example} simd_vectors.py
```

(simd-vectors)=
## Vectors: `float_4` and `int32_4`

Rack defines two vector types, the explicit specializations of its template
`Vector<TYPE, SIZE>`:

- `float_4` (`Vector<float, 4>`): four `float` lanes in one SSE register;
- `int32_4` (`Vector<int32_t, 4>`): four `int32_t` lanes.

Both are mutable Python values (copied on assignment of a new vector,
unhashable, `copy.copy` copies), with Rack's members:

| C++ | Python |
| --- | --- |
| `Vector()` | `float_4()`: all lanes 0. Rack leaves them uninitialized; RackPy zeroes them, as reading them would be undefined. |
| `Vector(float x)`, `Vector(int32_t x)` | `float_4(x)`, `int32_4(x)`: every lane `x`. |
| `Vector(x1, x2, x3, x4)` | the same, lane 0 first. |
| `Vector(__m128)`, `Vector(__m128i)` | `float_4(v)`, `int32_4(v)`: a copy. In Python, `float_4` and `int32_4` stand for the compiler's register types `__m128` and `__m128i`. |
| `float_4(int32_4)` | converts each lane to the nearest float (`_mm_cvtepi32_ps`). |
| `int32_4(float_4)` | converts each lane toward zero (`_mm_cvttps_epi32`); NaN and out-of-range lanes give `-2**31`. |
| `zero()`, `mask()` | static: all bits 0, all bits 1 (each `float_4` lane of a mask is a NaN). |
| `cast(a)` | static: reinterprets the other type's bits, with no conversion. |
| `size`, `type` | `4`, and the lane type as a Python class (`float`, `int`). |

`Vector` itself is `rackpy.raw.simd.Vector`, a template object:
`Vector[float, 4]` is `float_4` and `Vector[int, 4]` is `int32_4`. Rack
declares the primary template but never defines it, so any other arguments
raise `TypeError`, as they would fail to compile in C++.

Python numbers cross as C++ numbers: a float lane is rounded to 32 bits, an
`int32_4` lane takes an `int` in the `int32_t` range (`OverflowError`
otherwise) and refuses a float, as the [values](values.md#numbers-cross-as-c-numbers)
do. Integer lanes wrap on overflow, as SSE's additions do.

(simd-views)=
## Lanes, views and buffers

- `v[i]` reads lane `i` and `v[i] = x` writes it (`operator[]`). Indices are 0
  to 3, or -4 to -1 from the end; others raise `IndexError`, where C++ would
  read past the vector. `len(v)` is 4, `list(v)` and `v.tolist()` give the
  lanes.
- `v.s` is Rack's lane array (`s`, a union with `v`) as a live, writable
  `memoryview` (format `f` or `i`) that keeps the vector alive. Rack notes that
  touching single lanes "defeats the purpose of vectorizing"; that is about
  C++ speed, and does not matter for Python, which is slower anyway.
- `v.v` is the register (the union's other member) as a vector of the same
  type: reading copies it, and assigning a vector of the same type replaces
  every lane. A scalar is refused there, as C++ cannot assign a `float` to an
  `__m128`.
- The vectors support the buffer protocol: `memoryview(v)` is the same view
  as `v.s`.
- `float_4.load(x)` and `v.store(x)` take a buffer where C++ takes a pointer:
  a contiguous, one-dimensional buffer of `float32` items (`int32` for
  `int32_4`) with at least 4 of them, of any alignment (Rack uses unaligned
  loads and stores). Lane 0 is `x[0]`. Rack's comment that the order is
  reversed on little-endian machines is wrong: `_mm_loadu_ps` puts `x[0]` in
  lane 0, and the reference confirms it. A slice loads from an offset, like
  `&x[c]` in C++: `float_4.load(memoryview(buf)[c:])`. A shorter buffer
  raises `ValueError`, a buffer of another item type `TypeError`, a strided
  one `ValueError`, and storing into a read-only one `BufferError`.

(simd-operators)=
## Operators

Rack's free operators are Python's operators:

| | `float_4` | `int32_4` |
| --- | --- | --- |
| Arithmetic | `+ - * /`, unary `+ -` | `+ -`, unary `+ -` (no `*` or `/`: SSE has none) |
| Bitwise | `^ & \| ~` on the bits | `^ & \| ~` |
| Shifts | — | `<< n`, `>> n`: logical, and a count outside 0 to 31 gives 0 in every lane |
| Comparisons | `== != < <= > >=`: [masks](#masks) | the same |
| Compound | `+= -= *= /= ^= &= \|=` | `+= -= ^= &= \|=` |

A compound assignment changes the vector in place and returns it, as C++
returns a reference. Python has no `++` or `--`: `v.pre_increment()` and
`v.pre_decrement()` change the vector and return it (`++a`), while
`v.post_increment()` and `v.post_decrement()` change it and return the old
value (`a++`).

**Scalars broadcast; the two vector types never mix.** As C++'s non-explicit
`Vector(float)` does, a Python `float` or `int` becomes a `float_4` wherever
one is expected, on either side: `2.0 * v`, `1 - v`, `v / 4`. An `int`
becomes an `int32_4` (a `float` does not). But `float_4 + int32_4` raises
`TypeError`, because C++ rejects it as ambiguous: convert explicitly with
`float_4(i)` or `int32_4(f)`.

`DECLARE_VECTOR_OPERATOR_INFIX` and `DECLARE_VECTOR_OPERATOR_INCREMENT` are
the C++ macros that define these operators; see [macros](#macros).

(simd-masks)=
## Masks

A comparison returns a vector, not a `bool`: each lane is all ones (true) or
all zeros (false). That is Rack's design, and RackPy keeps it: combine masks
with `& | ~`, select with `ifelse(mask, a, b)`, and use them as bit masks, as
Rack's documentation suggests:

```text
x -= (x >= 1.0) & 1.0     # subtract 1 where x >= 1
```

A vector has no truth value, so `if a < b:` or `bool(v)` raises `TypeError`;
use `movemask` to get the lanes as bits. `==` against something that is not a
vector or a number gives Python's usual `False`.

(simd-functions)=
## Functions: `rackpy.raw.simd`

Each function has Rack's `float_4` overload and, as in C++, the scalar
functions `functions.hpp` imports (`using std::sin;`, `using math::clamp;`),
so a function written once works for floats and for vectors, as Rack intends
it to in C++ templates:

```text
def sin_plus_cos(x):       # x is a float or a float_4
    return simd.sin(x) + simd.cos(x)
```

The scalar overloads compute in C++ `float` (as a C++ template instantiated
with `float` would). A `float_4` argument picks the vector function, and a
scalar mixed with a vector broadcasts.

- **From instructions.** `andnot(a, b)` is `~a & b`. `movemask(a)` is an int
  whose bit `k` is lane `k`'s sign bit (`movemask(float_4.mask()) == 0xf`;
  note that `-0.0` counts). `rsqrt` and `rcp` are the CPU's fast
  approximations of `1/sqrt(x)` and `1/x` (relative error up to
  `1.5 * 2**-12` on x86); see [platforms](#platforms-and-implementations).
- **Selection.** `ifelse(mask, a, b)` takes `a`'s bits where the mask's are
  set and `b`'s elsewhere; `ifelse(cond, a, b)` with a `bool` is the scalar
  overload. `movemask_inverse` is Rack's function template, specialized by
  subscripting as in C++: `movemask_inverse[float_4](0b0101)` and
  `movemask_inverse[int32_4](...)` give a mask whose lane `k` is set when bit
  `k` is.
- **Math.** `fmax`, `fmin`, `sqrt`, `log`, `log10`, `log2`, `exp`, `sin`,
  `cos`, `tan`, `atan`, `atan2`, `trunc`, `floor`, `ceil`, `round`, `fmod`,
  `hypot`, `fabs`, `abs`, `pow`, and from `math.hpp` `clamp` (default 0 and
  1), `rescale`, `crossfade` and `sgn`. Where Rack's vector function differs
  from the C++ standard one, RackPy keeps Rack's:
  - `log`, `exp`, `sin`, `cos`, `tan`, `atan` and `atan2` use `sse_mathfun`'s
    approximations, and `log10`/`log2` divide its `log`. Their edge cases are
    not the standard ones: `log(x)` is NaN for `x <= 0`, zero included (not
    `-inf`); `exp` saturates at about `2.406e38`, which it returns for `inf`
    and NaN too; and `sin(±inf)` is `∓inf`, not NaN.
  - `round` rounds ties to even (`_mm_round_ps`); the scalar `std::round`
    rounds them away from zero.
  - `fmod(a, b)` is `a - floor(a / b) * b`, a floored modulus with the sign
    of `b`; the scalar `std::fmod` truncates.
  - `hypot` is `sqrt(a*a + b*b)`, without `std::hypot`'s overflow protection.
  - `fmax`/`fmin` return the second argument's lane where either lane is NaN.
  - `atan2(x, y)` is `std::atan2(x, y)`, the angle of the point `(y, x)`:
    Rack names the numerator `x`.
  - `pow(a, b)` for vectors is `exp(b * log(a))`, so where `a <= 0` it is
    `exp` of NaN, about `2.406e38`: `pow(float_4(0.0), 1/3)` is not 0.
- **`pow` with an `int` exponent** is Rack's template `pow<T>(T, int)` for
  `T = float` and `float_4`: `a**b` by repeated squaring. As in Rack, an
  exponent of 0 or less gives 1. Rack's loop doubles a C `int` past the
  exponent, which overflows (undefined, and in practice endless) for
  exponents from `2**30` on, so those raise `ValueError`.
- **Complex numbers.** `abs` and `arg` of `std::complex<float_4>` take a
  `(real, imag)` tuple of vectors; a Python `complex` is the imported
  `std::complex<float>` overload.

(simd-sse-mathfun)=
## `rackpy.raw.sse_mathfun`

`sse_mathfun.h` and `sse_mathfun_extension.h` are global-namespace C++
functions reached through `simd/functions.hpp`: Julien Pommier's SSE
approximations of `sin`, `cos`, `exp` and `log` from the cephes library, with
extensions for `tan`, `cot`, `atan` and `atan2`, as modified for Rack (zlib
licence). Their `__m128` arguments and results are `float_4`.
`sse_mathfun_sincos_ps(x)` returns `(sin, cos)`, which C++ writes through two
pointers; `sse_mathfun_tancot_ps(x, cot_flag)` is the tangent, or the
cotangent if `cot_flag` is nonzero; `sse_mathfun_atan2_ps(y, x)` takes `y`
first, like `std::atan2`; `sse_mathfun_one_ps()` is 1 in every lane.

(simd-ports)=
## Port voltages as vectors

`Port`'s SIMD templates (see [ports](engine-components.md#ports))
are bound for `T = float_4`, the only vector whose `load` and `store` take a
`float*`:

- `get_voltage_simd(first_channel)`: channels `first_channel` to
  `first_channel + 3`.
- `get_poly_voltage_simd(first_channel)`: the same if the port is
  polyphonic, else channel 0's voltage in every lane.
- `get_normal_voltage_simd(normal_voltage, first_channel)` and
  `get_normal_poly_voltage_simd(normal_voltage, first_channel)`: the same if
  the port is connected, else `normal_voltage` (a scalar broadcasts).
- `set_voltage_simd(voltage, first_channel)`: writes 4 channels; the channel
  count does not change.

`first_channel` must be 0 to 12, so the four channels stay inside the
16-channel array; Rack's templates would run past it, so other values raise
`IndexError`. Threads and ownership are those of the other port methods.

(simd-macros)=
## Macros

None of the SIMD headers' macros has a Python counterpart:

- `DECLARE_VECTOR_OPERATOR_INFIX` and `DECLARE_VECTOR_OPERATOR_INCREMENT`
  generate Vector's C++ operators; each operator they declare is its own
  record, mapped to a Python operator above.
- `SIMDE_ENABLE_NATIVE_ALIASES` is build configuration: on a CPU without
  SSE4.2, it tells SIMDe to provide the `_mm_*` intrinsic names.
- `_MM_FROUND_NO_EXC` is an intrinsic rounding flag that Rack defines from
  SIMDe's when the compiler does not. Python has no intrinsics.

The last two are absent on Linux x86_64, whose compiler provides SSE4.2.

(simd-platforms)=
## Platforms and implementations

`simd/common.hpp` chooses Rack's implementation at compile time: the CPU's
SSE4.2 instructions when the compiler targets them (Rack's x86-64 builds,
`-march=nehalem`), and the SIMDe library's emulation otherwise (Rack's arm64
builds). `rackpy.simd.IMPLEMENTATION` says which one this plugin has:
`"sse4.2"` for the only platform RackPy builds so far, Linux x86_64.

RackPy's tests also build the reference through Rack's SIMDe branch on x86-64,
using SIMDe's portable C code (`tests/simd/test_simde.py`), and compare the two
lane by lane. They agree exactly on all integer, bitwise, mask, comparison,
conversion, load and store results. Elsewhere they differ only in these ways,
which the test pins:

- the sign of zero results and NaN payloads, which are not Rack semantics;
- lanes where an input or the result is subnormal, because SSE's
  flush-to-zero applies differently to SIMDe's code;
- the last bits of the `sse_mathfun` functions and everything built on them
  (at most `1e-5` relative), and `sin(±inf)`, which SIMDe makes NaN;
- `rcp`, which SIMDe computes exactly, within the CPU's documented error;
- `rsqrt`, which SIMDe's portable code replaces with a bit-trick
  approximation that is wrong for 0, negative numbers and infinity.

**This is not evidence about arm64.** Rack's arm64 builds use SIMDe's NEON
code, not its portable code, and RackPy has not been built or run on arm64,
macOS or Windows. Each platform needs its own native run before RackPy claims
it.
