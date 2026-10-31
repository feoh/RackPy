# Find out what a plugin costs

A RackPy plugin spends time in two places: its native kernels, on Rack's audio
path, and its Python, on Rack's main thread. They are measured differently,
and only the first decides whether audio keeps up.

**Status:** Linux x86_64, Rack Free {{rack_version}}. The figures quoted are
RackPy's own measurements ([performance](../performance.md),
[ADR 004](../decisions/004-performance-budgets.md)), not guarantees for your
computer.

## The audio path: Rack's performance meters

In Rack, choose **Engine → Performance meters** (or press F3). Each module
shows how much of the audio time budget its processing takes, as for C++
modules. A RackPy module's figure is its kernel's cost plus RackPy's fixed
overhead, which ADR 004 measured at about 1 ns per frame on its test machine.

Python can read the whole engine's figure, the average and the peak share of
each block's time spent processing, from Rack's engine:
`rackpy.raw.APP().engine.get_meter_average()` and `get_meter_max()`.

If a module costs too much:

- **A graph** runs every node for every channel, every sample. Fewer nodes, or
  fewer channels, cost less. A graph [compiled into a kernel library](graphs.md#compile-graphs-into-a-kernel-library)
  avoids the interpreter's per-node dispatch.
- **A kernel in C++** ([process audio](dsp.md#write-a-kernel-in-c)) is as fast
  as the same code in a C++ module, and can use Rack's SIMD types to process
  four channels at a time ([polyphony](polyphony.md)).
- **Control-rate work** (coefficients, smoothing) need not run every sample:
  Rack's `ClockDivider` runs it every N frames, as the
  [DSP example](../../examples/dsp/README.md) does.

## The main thread: Python

Python runs when Rack calls it: building modules, drawing widgets, menus,
saving. Slow Python there makes Rack's interface less responsive, but does not
stop the audio, with one exception: Rack pauses audio while it runs a module's
reset, randomize, bypass and "load from JSON" handlers. RackPy records
`slow_exclusive_callback` in {py:func}`rackpy.runtime.diagnostics` for each that
takes over 1 ms.

To measure your own Python, time it with `time.perf_counter()` around the code
in question, in the callback itself, and log the result. A widget's `draw` runs
every frame for every visible module, so it is the usual place to look first;
a [framebuffer](draw-panels.md#a-cached-scale) draws once and is reused until
it is marked dirty.

## What RackPy measures about itself

RackPy's benchmarks, with their machines, commands and raw data, are in
[performance](../performance.md) and ADR 004. They measure RackPy, not your
plugin; `bin/perf-baseline` reproduces them in a checkout.
