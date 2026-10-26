# Process audio

**Implemented (RP-17A, RP-17B), Linux x86_64 inside Rack Free 2.6.6.** Two
built-in kernel types and kernels you write in C++. Rack's DSP helpers
(filters, FFTs, MinBLEP, resamplers, ring buffers, ...) are
[bound for Python](../reference/dsp.md) (RP-18, RP-19) for design, analysis
and tests, and its SIMD vectors [too](../reference/simd.md) (RP-20).
[DSP graphs](graphs.md) built in Python run as kernels too, through the
native plan interpreter or compiled into a kernel library (RP-21).

Rack calls every module's `process()` once per sample, tens of thousands of
times a second, on its engine thread. Python is far too slow and too
unpredictable for that ([performance budgets](../decisions/004-performance-budgets.md)),
so in RackPy the code that runs there is native: a **kernel**. Python picks a
kernel type, wires it to the module's ports, parameters and lights by index,
and hands it to the module:

```{rackpy-example} module_kernel.py
```

:::{note}
To try a `process()` written in Python, for an experiment or a test, use the
offline [callback laboratory](../explanation/callbacks.md). It never runs in
Rack's engine and is not real-time audio.
:::

## Kernels and their fields

`rackpy.dsp.kernel` has the kernel types. Calling one with keyword arguments
makes a kernel:

- `Gain(input=, output=, param=-1, gain=1.0)` multiplies each channel of the
  input by `gain`, and by the parameter's value if `param` names one. The
  output gets the input's channel count.
- `Sine(output=, pitch_input=-1, pitch_param=-1, amplitude=5.0)` is a sine
  oscillator for each channel of a V/oct pitch input (0 V is C4), at least
  one; `pitch_param` adds a parameter's value to the pitch.

A type's `fields` list what it takes: ports, parameters and lights by index
(`-1` for "none" where a field is optional), and numbers. `controls` are the
numbers you may change later. Bad fields raise `TypeError` or `ValueError`
when you create the kernel; `module.set_kernel(kernel)` then checks the
indices against the module's configuration (`ValueError` names the field),
so configure the module first.

## Changing a running module

`kernel.set("amplitude", 2.5)` changes a control. While Rack's engine runs
the module, the value goes through a bounded queue and reaches the kernel at
the next frame boundary, before it processes that frame. The queue holds
`CONTROL_CAPACITY` (256) values; if the engine has not consumed them,
`set` raises `ValueError` rather than wait. Parameters, ports and lights the
kernel reads change as usual, through Rack.

`module.set_kernel(other)` replaces the kernel at a frame boundary; the old
one is deleted on the main thread later, never on the audio thread. Its
Python handle stops working (`alive` is `False`). A kernel belongs to one
module: make a new one for each. `module.kernel` returns the current kernel.

## Reset, sample rate, bypass and failures

Rack's Initialize resets the kernel, and a sample-rate change reaches it
before the next frame; neither runs Python. A bypassed module does what Rack
does for bypass (its bypass routes), and the kernel is not called. If a
kernel reports a failure, RackPy silences the module's outputs, records a
`kernel_failed` diagnostic (`rackpy.runtime.diagnostics()`), and stops calling
it; `module.kernel_failed` is then `True` until Initialize or `set_kernel`.

## What it costs

On the planning machine (not controlled hardware; the raw data and
environment are in `benchmarks/rp17-kernel-overhead.json`), RackPy's kernel
path costs **0.64 ns per frame** on top of the same DSP written as a plain
C++ module for a sine oscillator (+7.6%), and 0.80 ns for a one-multiply
gain. That is within the budgets frozen in
[ADR 004](../decisions/004-performance-budgets.md) (at most 1.5 ns, and 10%
for kernels at least as costly as a sine). The built-in kernels do more than
that reference (polyphony, an optional parameter), which adds about 1.5 ns
per frame on these workloads.

## Threads

Create kernels, call `set_kernel` and `set` on Rack's main thread
(`ThreadError` otherwise). The engine thread only runs the kernel. A test in
RackPy's suite runs the kernel path under an allocation and lock detector
and requires zero heap, lock, write or GIL calls on it.

## Write a kernel in C++

When the built-in kernels are not enough, write your own in C++ and ship it
in your plugin as a **kernel library**. RackPy loads the libraries listed in
`rackpy-loader.json` before your entry module runs:

```json
"kernels": ["kernels/kernels_example.so"]
```

A library includes RackPy's `rackpy/kernel.hpp` (from RackPy's
`native/include`) after `<rack.hpp>`, implements `rackpy::Kernel`, and
registers its types with `RACKPY_KERNEL_LIBRARY`. It may also add node types
for [DSP graphs](graphs.md#native-extension-nodes) with `rackpy/graph.hpp`
and `RACKPY_GRAPH_NODES`. This wavefolder, with a graph node version of the
same fold and a sample-and-hold node, is the example RackPy's tests build and
run in Rack:

```{literalinclude} ../../tests/dsp/kernel_library/fold.cpp
:language: c++
```

Python then uses it like a built-in type, under the library's namespace:
`rackpy.dsp.kernel.load("example.Fold")` returns the type, and
`module.set_kernel(Fold(input=0, output=0, limit=5))` gives a module the
kernel (RackPy's test `tests/dsp/in_rack_libraries/test_libraries.py` does
exactly this inside Rack).

Build it as you would a Rack plugin: with the Rack SDK's compiler flags
(`-fPIC -O3 -march=nehalem -funsafe-math-optimizations` and the rest of
`compile.mk`), `-I` the SDK's `include` and `dep/include` and RackPy's
`native/include`, linked as a shared library against `-lRack` with
`-static-libstdc++ -static-libgcc`, as for any plugin. RackPy's own build does
exactly that for the example (`rackpy_kernels_example` in `CMakeLists.txt`).

The rules:

- `process`, `reset`, `sampleRateChange` and `setControl` run where audio
  is produced: no allocation, locks, IO, logging or exceptions (they are
  `noexcept`). Allocate in the constructor. Return `false` from `process` to
  fail, as described above.
- The constructor runs on the main thread when Python creates the kernel. If
  it throws, Python gets `ValueError` with your message.
- Names are identifiers. Python sees `namespace.Name`. `rackpy` and names
  starting with `rackpy_` are reserved.
- A library loads all or nothing, kernel types and graph node types
  together. If it cannot be loaded, was built for another kernel ABI
  (`rackpy::KERNEL_ABI`) or graph ABI (`rackpy::graph::GRAPH_ABI`), defines
  only part of `RACKPY_GRAPH_NODES`, throws while registering, repeats a
  type name, uses a reserved namespace or a bad name, gives a node type more
  than 8 inputs or repeated argument names, or its path leaves the plugin
  folder, Rack's log says `RackPy skipped kernel library ...` with the
  reason, none of its types appear, and the other libraries still load.
- Libraries stay loaded until Rack exits. They are trusted code from your
  plugin, like your Python.
