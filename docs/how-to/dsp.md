# Process audio

**Implemented (RP-17A), Linux x86_64 inside Rack Free 2.6.6.** Two built-in
kernel types so far; kernel libraries of your own are RP-17B, and DSP building
blocks (filters, oscillators, MinBLEP, ...) come with RP-18 to RP-21.

Rack calls every module's `process()` once per sample, tens of thousands of
times a second, on its engine thread. Python is far too slow and too
unpredictable for that ([performance budgets](../decisions/004-performance-budgets.md)),
so in RackPy the code that runs there is native: a **kernel**. Python picks a
kernel type, wires it to the module's ports, parameters and lights by index,
and hands it to the module:

```{rackpy-example} module_kernel.py
```

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

## Threads

Create kernels, call `set_kernel` and `set` on Rack's main thread
(`ThreadError` otherwise). The engine thread only runs the kernel. A test in
RackPy's suite runs the kernel path under an allocation and lock detector
and requires zero heap, lock, write or GIL calls on it.
