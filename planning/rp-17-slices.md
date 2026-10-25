# RP-17 bounded implementation slices

These child contracts supplement, not replace, [RP-17 in TASKS.md](TASKS.md).
Live Witan status is authoritative.

**Owner decisions 2026-10-24:**

- A module's kernel is a native object set in the constructor:
  `m.set_kernel(Gain(input=0, output=0, param=0))`, with its wiring checked
  then. `set_kernel` from the main thread later replaces it at a frame
  boundary, and `kernel.set(name, value)` sends controls through a bounded
  SPSC queue that raises when full.
- Custom kernels are shared libraries in the plugin, built with the Rack SDK
  against RackPy's kernel header and listed in `rackpy-loader.json`. The host
  loads them at init after an ABI check and registers their types under a
  namespace.

**RP-17A — native kernel runtime, built-in Gain and Sine, Module.set_kernel**
(`tk-rackpy-17a-native-kernel-runtime-built-in-gain-a-32ae54`):

- **Interface.** `native/include/rackpy/kernel.hpp` is the public interface:
  `Kernel` (a noexcept `process` that returns a failure flag, plus `reset`,
  `sampleRateChange` and `setControl`), and `KernelType`, whose fields are
  ports, params, lights and numbers, with named controls.
- **Runtime.** `native/dsp/runtime/` gives each module a `KernelSlot`. Each
  frame it picks up a kernel handed over through an atomic pointer, retiring
  the old one to an engine-to-main ring that the main thread reclaims. It
  then drains a 256-entry control ring addressed to the current kernel and
  runs the kernel. On failure it silences the outputs and records a
  lock-free `kernel_failed` diagnostic.
- **Rack events.** Reset and sample-rate changes are forwarded from Rack's
  events. Bypass stays Rack's.
- **Built-in kernels.** `rackpy.Gain` and `rackpy.Sine` are polyphonic and
  bit-identical to direct C++ (RP-07's reference) over 100,000 frames.
- **Python API.** `rackpy.dsp.kernel` has the types, `Kernel` handles and
  `set`, plus `Module.set_kernel`, `Module.kernel` and `Module.kernel_failed`.
- **Instrumentation.** The RP-07 audio guard moved to
  `native/testing/audio_guard.c`. A guard run shows zero forbidden calls on
  the kernel path and catches an injected allocation. The kernel and
  lifecycle objects reference no CPython symbols.
- **Records.** `Module::process` stays planned: its Python override is
  RP-22's laboratory.

**Still on the parent:** custom kernel libraries (RP-17B, as decided above),
and native bypass kernels if a module needs custom bypass behaviour. DSP
algorithms are RP-18 and RP-19; graphs are RP-21.
