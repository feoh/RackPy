# RP-17B validation and RP-17 performance (Linux x86_64)

Date 2026-10-24, on `main` after d9cc52a. Pinned inputs: GCC 16.2.1, uv
0.9.18, Rack SDK and Rack Free 2.6.6 (061ccf63), CPython 3.14.9, pybind11
3.1.0, compiled with the SDK's flags. Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds the six test kernel libraries; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | 197 passed, 31 deselected (318 s); inventory 29 passed; prek and `sphinx-build -W` pass |
| `uv run pytest -q tests/dsp` | 6 passed (7 with the benchmark smoke test), including `test_kernel_libraries_load_and_bad_ones_are_skipped` (actual Rack, live UI) |
| `LD_PRELOAD=build/hooks/audio_guard.so build/hooks/rackpy_kernel_guard` | after the hot-path change: production and failure paths all zero over 100,000 frames each; injected malloc and free caught 100,000 times each |
| `for core in 4 6 8; do taskset -c $core build/hooks/rackpy_kernel_bench; done` | see below; raw data in `benchmarks/rp17-kernel-overhead.json` |

Kernel libraries, in actual Rack:

- `example.Fold`, from `kernels/kernels_example.so`, follows folded
  voltages exactly through a real cable, including after a `gain` control.
- The library's own constructor exception reaches Python as `ValueError`.
- These are skipped, each with its reason in Rack's log:
  - a wrong ABI (999);
  - a library that throws while registering (its earlier type is not
    added);
  - a duplicate `example.Fold`;
  - the reserved namespace `rackpy`;
  - a bad type name;
  - a missing file;
  - a `..` path;
  - a symlink that resolves outside the plugin folder;
  - a non-string entry.

RackPy's per-frame cost against plain C++ modules, as nanoseconds per
`process()` call: median of 31 randomized A/B repetitions of 512,000 frames,
on P-cores 4, 6 and 8. This is the planning machine (i9-14900K, `powersave`
governor, load average 8.1 during the run), not controlled hardware.

| Workload | Native p50 | RackPy p50 | Overhead p50 | Overhead p99 |
| --- | --- | --- | --- | --- |
| identical sine DSP in a kernel | 8.38 | 9.02 | **+0.64 ns (+7.6%)** | +0.69 |
| identical gain DSP in a kernel | 0.75–0.79 | 1.56–1.59 | **+0.79–0.82 ns** | +0.95 |
| built-in `rackpy.Sine` (polyphonic) vs the mono reference | 8.39 | 10.54 | +2.15 ns | +2.25 |
| built-in `rackpy.Gain` (polyphonic, optional param) vs the mono reference | 0.90 | 2.97–3.02 | +2.13–2.18 ns | +2.34 |

The fixed cost is within ADR 004's frozen budget (at most 1.5 ns, and at most
10% p50 and 20% p99 for kernels costing at least a sine), and below RP-07's
spike shape (+0.87 ns). Before the hot path became one fast check, the same
benchmark measured +0.99 ns (+11.8%) for the sine, over the 10% budget. The
built-in kernels' extra cost is their extra work, which a polyphonic C++
module would also do. RP-43 repeats this on controlled hardware.
