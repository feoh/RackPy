# RP-17A validation (Linux x86_64)

Date 2026-10-24, on `main` after 60a8ba6. Pinned inputs: GCC 16.2.1, uv
0.9.18, Rack SDK and Rack Free 2.6.6 (061ccf63), CPython 3.14.9, pybind11
3.1.0, compiled with the SDK's flags (`-O3 -march=nehalem
-funsafe-math-optimizations`). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass, including the new `kernel_runtime` |
| `uv run pytest -q -m 'not dist and not inventory'` | 195 passed, 31 deselected (315 s), including the RP-07 perf-guard tests with the guard's new path and the `module_kernel.py` snippet in Rack; inventory 29 passed; prek and `sphinx-build -W` pass |
| `uv run pytest -q tests/dsp` | 5 passed. Native equivalence and handover; the guard run; the guard requiring its preload; no CPython symbols in the kernel and lifecycle objects; the in-Rack suite (14 tests) |
| `LD_PRELOAD=build/hooks/audio_guard.so build/hooks/rackpy_kernel_guard` | production and failure paths: every count is 0 over 100,000 frames each; injected kernel: malloc 100,000, free 100,000 |

What the tests establish:

- `rackpy.Gain`, over 100,000 frames of 1 to 16 channels, and `rackpy.Sine`,
  with 1, 4 and 16 channels, write bit-identical outputs to the same
  arithmetic written as direct C++.
- On one thread, the handover behaves as designed:
  - a kernel waiting to start takes controls directly;
  - it is told the sample rate before its first frame;
  - the queue accepts exactly 256 values, then refuses;
  - a failing kernel silences the module and records one diagnostic;
  - reset clears the failure;
  - a replaced kernel is deleted only by the main thread's `reclaim()`;
  - a waiting kernel replaced before the engine took it is deleted at once.
- Inside Rack's engine:
  - a Gain module fed through a real cable follows its parameter and its
    `gain` control (sent through the queue);
  - a Sine module changes amplitude through its control;
  - Rack's sample-rate change and reset reach the kernel;
  - bypass stops calling it;
  - a kernel failure silences the module and records `kernel_failed`, and
    reset recovers it;
  - 300 replacements under load and teardown with a kernel still pending
    leave stale handles, not crashes;
  - kernels work from a `ModuleClass`;
  - wiring, field and thread errors are refused.

Found and fixed during validation: a handle obtained through `Module.kernel`
had no field values, so `set()` wrote past them and crashed Rack. Values now
live on the native side.

## Gates not established

There are no timing measurements here. RP-07's budget (≤ 1.5 ns per frame of
RackPy overhead) was measured on the spike's shape; the production
`KernelSlot` adds an atomic load for kernel pickup next to the spike's queue check. Re-measuring belongs with
`bin/perf-baseline` on controlled hardware (RP-43). macOS, Windows and the
distributable build are not covered.
