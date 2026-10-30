# Values: math, colours, quantities and common helpers

Family 1 of the [SDK surface](surface/common.md): `math.hpp`, `color.hpp`,
`Quantity.hpp`, `common.hpp` and `arch.hpp` (RP-12). The string, random,
logger, mutex and weak-pointer utilities in the same family belong to the
services task (RP-30).

Every function here runs **Rack's own C++ code**, compiled with the SDK's
flags. `tests/values` replays more than 7,000 cases from a compiled C++
reference through these bindings, inside Rack, and requires the results to be
identical bit for bit, so nothing here is a Python re-implementation that
could drift from Rack.

The low-level names are in `rackpy.raw` (C++ namespace for namespace:
`rack::math` is `rackpy.raw.math`); {py:mod}`rackpy.math` and
{py:mod}`rackpy.types` are the tidy imports for everyday use.

(values-numbers)=
## Numbers cross as C++ numbers

- A Python `float` becomes a C++ `float`: it is rounded to 32 bits, and a
  magnitude beyond the float range becomes `±inf`, exactly as a C++ cast does.
  `rackpy.math.crossfade(0.1, 0.1, 0)` is `0.10000000149011612`.
- A Python `int` becomes the C++ integer type the function takes. A value that
  does not fit raises `OverflowError`; it never wraps silently and never falls
  through to a float overload.
- NaN and infinities pass through with Rack's semantics: `rescale` over an
  empty range divides by zero, giving `±inf` (or NaN when `x` is the range's
  start), as Rack's does.
- **The sign of a zero is not part of Rack's semantics.** Rack and RackPy are
  compiled with `-funsafe-math-optimizations`, which implies
  `-fno-signed-zeros`, so a function may return `0.0` or `-0.0` for the same
  input depending on the compiler and call site: `clamp(-0.0)` is `0.0` from
  one build and `-0.0` from another. Compare results with `==`, which treats
  them as equal. `normalize_zero` is the exception: Rack compiles it to keep
  signed zeros, and it always returns `+0.0`.
- **Denormals are flushed to zero inside Rack.** Rack's `main()` sets the CPU's
  flush-to-zero and denormals-are-zero modes (`system::resetFpuFlags()`), and
  threads inherit them, so in a Rack process *all* float arithmetic, including
  plain Python `float` arithmetic, treats subnormal numbers as zero:
  `math.ldexp(1.0, -1070)` is `0.0` inside Rack and about `7.9e-323` outside it.
  The C++ reference test runs in the same mode.

(values-scalars)=
## Scalar functions

`clamp`, `clamp_safe`, `euc_mod`, `sgn` and the other helpers keep every C++
overload. Python picks one by argument type: all-`int` arguments reach the
`int` overload, anything else the `float` one, so `clamp(15, 0, 10)` is the
integer `10` and `clamp(1.5)` uses the float defaults 0 and 1.

The template helpers take these instantiations (RP-31K, at the owner's
request of 2026-10-27):

| Function | `T` | Outside it |
| --- | --- | --- |
| `is_even`, `is_odd`, `is_pow2` | `int64_t` for −2⁶³ to 2⁶³ − 1, `uint64_t` for 2⁶³ to 2⁶⁴ − 1 | `OverflowError` |
| `sgn(x)` for an `int` | `int64_t` | `OverflowError` |
| `sgn(x)` for a `float` | `float`, or `double` with `type="double"` | `ValueError` for another `type` |

For a value that several C++ integer types can hold, every one of them gives
the same result, so these two reach every result Rack's integer `T` can; a
Python float is a C++ double, which a `long double` holds exactly, so `float`
and `double` do the same for floating-point `T`. `is_pow2(2**63)` is `True`,
as `isPow2<uint64_t>(1ull << 63)` is in C++.
`sgn` converts its argument to a C++ float unless `type="double"`: a float
cannot hold 1e-50, so `sgn(1e-50)` is `0.0` and `sgn(1e-50, type="double")`
is `1.0`.

Where Rack only documents a precondition, RackPy checks it, because breaking
it in C++ would crash or hang Rack:

| Function | Rack's rule | RackPy raises |
| --- | --- | --- |
| `euc_mod`, `euc_div`, `euc_div_mod` (int) | `b` must be positive | `ValueError` |
| `log2` | (a negative `n` loops forever) | `ValueError` for `n < 0` |
| `interpolate_linear(p, x)` | `p` must have `floor(x) + 2` values | `IndexError`, also for negative or NaN `x` |

Out-parameters become return values: `euc_div_mod(-7, 3)` is `(-3, 2)` and
`complex_mult(ar, ai, br, bi)` returns `(cr, ci)`. `interpolate_linear` copies
`p` and never modifies it.

(values-vec)=
## Vec

`Vec(x, y)` is a 2D vector of C++ floats with Rack's methods (`plus`, `norm`,
`rotate`, …; `mult` and `div` take a `Vec` or a float, as their two C++
overloads do) and Rack's operators: `+`, `-`, `*` and `/` with a `Vec` or a
float, unary `+` and `-`, `==` and `!=`, and the in-place forms, which change
the `Vec` itself. `v[0]` and `v[1]` are `x` and `y`; any other index raises
`IndexError` (C++ would quietly return `y`).

A `Vec` is a **mutable value**: assigning it or passing it to a function copies
it, as in C++, and `copy.copy` makes an independent copy. Being mutable, it is
not hashable.

(values-rect)=
## Rect

`Rect(pos, size)` or `Rect(x, y, width, height)` includes its left and top
edges but not its right and bottom ones. `Rect.inf()` is the whole plane.
It has Rack's methods, which return new values: the static constructors
`from_min_max` and `from_corners`; the getters (`get_left`, …, `get_center`,
`get_top_left`, `get_top_right`, `get_bottom_left`, `get_bottom_right`);
`contains`, which takes a `Vec` or a `Rect`, `intersects` and `equals`;
`clamp`, `nudge`, `expand`, `intersect`, `zero_pos`, `grow`, `shrink` and
`interpolate`; and Rack's aliases `is_containing`, `is_intersecting` and
`is_equal`. `==` and `!=` are Rack's operators. The facade adds read-only
properties (`left`, `right`, `top`, `bottom`, `width`, `height`, `center`)
over Rack's getters.

(values-views)=
## Copies and views

The one rule that matters: **a field read from a Rack object is a view of
that field, not a copy.**

```text
r = Rect(1, 2, 3, 4)
r.pos.x = 10      # changes r: r.pos is a view of r's field
p = r.pos         # still a view; p keeps r alive
p = copy.copy(r.pos)   # an independent Vec
r.pos = Vec(7, 8) # assignment copies the new value into r
```

Methods and operators return new values. RackPy never hands out a copy where
C++ returns a reference: when a later family binds a widget's `box`, writing
`widget.box.pos.x` will move the widget, exactly as in C++.

(values-color)=
## Colours

`NVGcolor(r, g, b, a=1.0)` (also `rackpy.types.Color`) is nanovg's colour, which
Rack passes by value everywhere. It is therefore an **immutable** value: its
components are read-only, it is hashable, and the constants in
`rackpy.raw.color` (`RED`, `WHITE_TRANSPARENT`, …) cannot be changed by
accident. The `rackpy.raw.color` functions are Rack's: `lerp`, `screen`,
`alpha`, `mult` (by a colour or a float), and so on.

`from_hex_string` behaves as Rack's implementation does, which is not what its
header says. `color.hpp` documents "returns WHITE on error", but the code reads
the text with `sscanf` and returns whatever it parsed: opaque black for text it
cannot read at all, and a partial colour for partial input (`"#ff"` is opaque
red). It never raises. RackPy follows the implementation, and the C++ reference
test checks that it does.

(values-quantity)=
## Quantity

`rackpy.raw.Quantity` is `rack::Quantity`, the value-with-bounds-and-labels
that knobs and sliders display. What a Python subclass may override depends on
where Rack calls each method ([callback matrix](callback-matrix.md)):

| Methods | Rack calls them | In Python |
| --- | --- | --- |
| `get_display_value`, `set_display_value`, `get_display_precision`, `get_display_value_string`, `set_display_value_string`, `get_label`, `get_unit`, `get_string` | on the UI thread | override freely |
| `reset`, `randomize` | sometimes with audio stopped | override, but keep it brief (ADR 004: 1 ms) |
| `get_value`, `set_value`, `get_min_value`, `get_max_value`, `get_default_value` | also on the **engine thread** (Core MIDI-Map) | **cannot** be overridden: a subclass that defines one raises `TypeError` |

Outside the [callback laboratory](../explanation/callbacks.md#laboratory-quantities),
whose `rackpy.laboratory.Quantity` lets an offline experiment override them,
the value methods cannot be Python, so use
{py:class}`rackpy.types.ValueQuantity`: RackPy's own Quantity, which keeps its
value, bounds and default natively, so Rack can read them from any thread,
while Python sets them and overrides the display methods. Like Rack's
`ParamQuantity`, it clamps a new value to its bounds with `clamp_safe`. Because
`fmin(NaN, max)` is `max`, a NaN becomes the **maximum**. `set_display_value_string` accepts Rack's formula
language (`"c4"`, `"1+1"`, `"dbtogain(-6)"`) and leaves the value unchanged if
the text does not parse.

When Rack calls a Python override and it raises, the error is caught at the
boundary, logged and recorded as a `callback_raised` diagnostic, and Rack's
default answers instead. When your own Python code calls the method, the error
reaches you as usual.

(values-exception)=
## Exception

`rackpy.raw.Exception` is `rack::Exception`. Rack's C++ exceptions arrive in
Python as this type, a `RuntimeError`, with `msg` and `what()` giving the
message. The printf-style constructor is `Exception.printf(format, *args)`,
which formats with Python's `%` after removing C length modifiers, so
`printf("%lld ports", 3)` reads `3 ports`.

(values-common)=
## Configuration strings and helpers

| C++ | Python |
| --- | --- |
| `APP_NAME`, `APP_VERSION`, `APP_OS`, `APP_CPU`, `API_URL`, … | `rackpy.raw.APP_NAME`, … (the running Rack's values) |
| `bitCast<To, From>(x)` | `rackpy.raw.bit_cast(x, to, from_)` with type names `i8` … `u64`, `f32`, `f64`; different sizes raise `ValueError`, as they fail to compile in C++ |
| `42_u8`, `0.5_f32`, … | `rackpy.raw.literals.u8(42)`, `rackpy.raw.literals.f32(0.5)`, … with C++'s conversions (`u8(256)` is 0) |
| `construct<T>(&T::field, value, …)` | `rackpy.raw.construct(cls, field=value, …)` |
| `get(map, key, def)`, `get(vector, i, def)` | `rackpy.raw.get(container, key, default=None)`: never inserts; an index outside the sequence gives `default` |
| `DeferWrapper`, `deferWrapper(f)`, `DEFER({...})` | `with rackpy.raw.defer_wrapper(f):` or `with rackpy.raw.macros.DEFER(f):` runs `f` when the block exits |

C++ value-initializes `get`'s default (`0`, `""`); Python cannot know the
container's element type, so its default is `None`.

(values-macros)=
## Macros

Each of Rack's macros has a reviewed disposition (coverage policy 9):

| Macro | Python |
| --- | --- |
| `ARCH_LIN`, `ARCH_X64` (and `ARCH_WIN`, `ARCH_MAC`, `ARCH_ARM64` on their platforms) | constants in `rackpy.raw.macros`, defined only on the platform the plugin was built for |
| `VEC_ARGS(v)`, `RECT_ARGS(r)` | `macros.VEC_ARGS(v)` is `(v.x, v.y)`; `macros.RECT_ARGS(r)` is the four floats |
| `LENGTHOF(arr)` | `macros.LENGTHOF(arr)`, which is `len(arr)` |
| `ENUMS(name, count)` | `macros.ENUMS(name, count, start)` returns `{name: start, name_LAST: start + count - 1}` |
| `DEFER(code)` | `macros.DEFER(f)`, a context manager |
| `DEPRECATED` | `macros.DEPRECATED`, a decorator whose target emits `DeprecationWarning` |
| `CONCAT`, `CONCAT_LITERAL`, `TOSTRING`, `TOSTRING_LITERAL` | **no runtime meaning**: they paste and quote tokens for the preprocessor; Python builds strings directly |
| `BINARY`, `BINARY_START`, `BINARY_END`, `BINARY_SIZE` | **no runtime meaning**: they name data the linker embeds in a C++ plugin; a Python plugin reads its files through asset paths (RP-30) |

(values-json)=
## JSON

Rack's state and settings APIs exchange jansson `json_t`. RackPy never hands
Python a `json_t`; it converts at the boundary, in both directions, with one
policy (`native/runtime/json.cpp`), described by `rackpy.types.JSON`:

- `null`, booleans, integers, reals, strings, arrays and objects become `None`,
  `bool`, `int`, `float`, `str`, `list` and `dict`; tuples are accepted as arrays.
- Integers must fit jansson's 64-bit `json_int_t` (`OverflowError` otherwise).
- NaN and infinities raise `ValueError`: JSON cannot represent them.
- Object keys must be `str` (`TypeError`). Neither keys nor strings may
  contain NUL characters (`ValueError`): jansson writes NUL as `\u0000`, but
  Rack loads patches, presets and the clipboard without `JSON_ALLOW_NUL`, so it
  would refuse the whole file (RP-15C).
- From `json_t`, strings that are not valid UTF-8 raise `UnicodeDecodeError`.
  jansson's own loader refuses them; only a plugin's `json_string_nocheck`
  can make one.
- Anything nested more than 64 levels deep, which includes a list that
  contains itself, raises `ValueError`.

Later families use this for `dataToJson`, `paramsFromJson`, plugin settings and
so on (RP-15).

(values-boundaries)=
## Buffers and opaque handles (design for later families)

Two more boundary policies, designed here and implemented where they first
appear:

- **Buffers.** Native arrays (a port's 16 voltages, a DSP block) reach Python
  through the buffer protocol only from an object that owns or keeps alive the
  storage: a snapshot copy, or a view that is valid only inside the callback
  that provided it and raises afterwards (ADR 003, decisions 8 and 10). RP-13
  implements port voltage views.
- **Opaque handles.** Pointers Python must not dereference (`GLFWwindow*`,
  `NVGcontext*`, `Context::patch`) become handle objects that support identity
  and a null check and nothing else, never an integer address (coverage
  policies 4, 8 and 13): `rackpy.raw.window.NativeHandle` ([drawing and the
  window](window.md#the-context)).
