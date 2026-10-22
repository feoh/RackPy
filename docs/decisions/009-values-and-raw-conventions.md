# ADR 009: Raw naming, values and views, and tests inside Rack

- **Status:** Accepted.
- **Date:** 2026-10-22
- **Task:** RP-12
- **Evidence:** [tests/values](../../tests/values/test_values_in_rack.py),
  [native/tests/values_reference.cpp](../../native/tests/values_reference.cpp),
  [values reference](../reference/values.md)

## Context

RP-12 is the first family of real bindings. Its conventions are the ones every
later family follows, so they are recorded here rather than in one family's
reference page.

## Decisions

1. **`rackpy.raw` mirrors Rack's namespaces.** `rack::X` is `rackpy.raw.X`,
   `rack::math::Y` is `rackpy.raw.math.Y`, and so on. Global-namespace items
   get a named module (`literals` for user-defined literals, `macros` for
   macros), and dependency types Rack's signatures need live under the
   dependency's name (`rackpy.raw.nanovg.NVGcolor`). The generated registry
   passes the root `raw` module to each family adapter, so families can share
   a namespace.
2. **Names.** Classes, enums and constants keep their C++ names; functions,
   methods and parameters are snake_case (`getLeft` is `get_left`). Each C++
   overload stays reachable by argument type, and int overloads accept Python
   ints only, raising `OverflowError` outside the C++ type rather than wrapping
   or falling through to a float overload. Operators map to Python's operator
   methods. Out-parameters become return values.
3. **Rack's code, not a copy of it.** Bindings call Rack's functions (inline
   ones compiled with the SDK's flags). A compiled C++ reference evaluates the
   same functions over edge cases and the bindings must match it bit for bit
   inside Rack; for family 1 that is 7,214 cases.
4. **Preconditions become exceptions.** Where Rack only documents a
   precondition whose violation crashes or hangs (a non-positive divisor, a
   negative `log2`, an out-of-range array index), RackPy checks it first and
   raises `ValueError` or `IndexError`.
5. **Values and views.** A value passed by value in C++ is a Python value:
   copied on assignment, compared by value, unhashable if mutable. A field read
   from an object is a live view of that field (pybind11's
   `reference_internal`), which keeps its owner alive and writes through, as in
   C++. Value types that C++ only ever passes by value and that have no fields
   Rack exposes for writing (`NVGcolor`) are immutable and hashable, so shared
   constants cannot be changed by accident.
6. **Callback strategies shape subclassing.** A virtual method whose RP-06
   strategy is `mixed` or `engine` gets no Python path: the trampoline does not
   override it, and the Python class refuses a subclass that defines it. Where
   Python still needs to configure such behaviour, RackPy provides a native
   implementation that Python configures (`ValueQuantity`). Overrides that Rack
   calls without the GIL go through `callPython`; overrides that Python code
   calls propagate exceptions normally. The trampoline looks overrides up by
   the most-derived bound type, which pybind11 requires.
7. **Macros** each get a disposition: a constant, an equivalent function or
   context manager, or `no_runtime_meaning` (a manifest kind allowed only for
   macros, with a reason and a docs anchor) for preprocessor- and linker-only
   macros.
8. **Boundary policies.** JSON converts at the boundary with one strict
   policy (`native/runtime/json.cpp`): no NaN or infinity, 64-bit integers,
   `str` keys, at most 64 levels. Buffers and opaque handles are designed in
   the values reference and implemented by the families that first need them.
9. **Tests run inside Rack.** `tests.support.run_pytest_in_rack` copies
   pytest and its locked dependencies into a staged *test* bundle and runs a
   suite (a directory named `in_rack`) inside Rack's embedded interpreter, in
   the built plugin, returning a result per test. The outer pytest run ignores
   `*/in_rack/*`; one host-marked test per family starts the suite. Each family
   suite runs on the test-hooks build (which can also exercise paths only Rack
   takes) and on the production build, where only hook-dependent tests skip.
10. **Records are `implemented`, not `verified`,** until someone other than
    their author reviews them. The coverage contract asks for a reviewer for
    `verified`, and a self-review is not one.

## A finding that affects all Python in Rack

Rack's `main()` calls `system::resetFpuFlags()`, which turns on the CPU's
flush-to-zero and denormals-are-zero modes; engine threads do the same, and
threads inherit the mode. Inside a Rack process, **all** floating-point
arithmetic, plain Python `float` arithmetic included, therefore treats
subnormal numbers as zero. The first differential run disagreed with Rack on
exactly two cases, both with a denormal input, because the C++ reference ran in
the default mode. The reference now sets the same flags, and the values
reference documents the behaviour for users.

## Errata found in Rack's headers

- `color.hpp` says `fromHexString` "returns WHITE on error". Rack's
  implementation (`src/color.cpp`) uses `sscanf` and returns whatever it
  parsed: opaque black for unparseable text. RackPy follows the implementation.
