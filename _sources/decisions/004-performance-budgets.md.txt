# ADR 004: Performance and real-time budgets

- **Status:** Accepted as the working budgets for implementation. Of two
  revisions to the candidate budgets in [performance](../performance.md), the
  owner approved the graph-plan budget on 2026-10-26; the `ui-exclusive`
  budget still needs **owner approval** (marked below). Every number comes from the planning
  machine, which is **not** controlled hardware; RP-43 repeats them on
  controlled hardware with an audio interface before any performance claim.
- **Date:** 2026-10-21
- **Task:** RP-07
- **Evidence:** [benchmarks/baseline.json](../../benchmarks/baseline.json)
  (raw data and environment), [benchmarks/spike](../../benchmarks/spike/README.md),
  [tests/integration/test_perf_guard.py](../../tests/integration/test_perf_guard.py)

## Method

`bin/perf-baseline run` rebuilds everything from pinned inputs and writes
`benchmarks/baseline.json` in about 7 minutes. It measures:

- **Microbenchmarks.** Plain C++ modules against the RackPy production shape:
  a generic module that drains an (empty) SPSC command queue and calls a native
  kernel, which Python chose and configured, through a virtual interface. Both
  sides use the same DSP source and the SDK's flags (`-O3 -march=nehalem
  -funsafe-math-optimizations`). Each `process()` call is one frame, as in Rack;
  nothing is batched. 21 repetitions of 2,000 blocks of 256 frames per variant,
  in randomized order, repeated on P-cores 4, 6 and 8. Numbers are nanoseconds
  per `process()` call.
- **Equivalence.** 200,000 frames, compared bit for bit.
- **An audio-path guard.** An `LD_PRELOAD` library counts malloc/free and
  friends, `pthread_mutex_lock`, `write` and `PyGILState_Ensure` while a thread
  is marked as on the audio path.
- **The Python laboratory.** A Python subclass of a native kernel, called once
  per frame through a pybind11 trampoline, which is the RP-22 shape.
- **Real host.** Rack Free 2.6.6 headless, pinned to P-cores 4–7, reading Rack's
  own engine meter (time spent in `stepBlock` as a fraction of real time). 1, 16
  and 64 modules at 44.1, 48 and 96 kHz, 3 repetitions in randomized order. Plus
  a two-module cable feedback loop, and slow-UI and exclusive-lock checks in
  windowed Rack under Xvfb.

Environment: Intel Core i9-14900K (hybrid P/E cores), Linux 7.2.11 CachyOS,
GCC 16.2.1, `powersave` governor, `balance_performance` energy preference, turbo
on, load average about 0.6. These were left as found.

## Results

### RackPy's fixed cost on the audio path

| Workload | Native p50 | RackPy p50 | Overhead p50 | Overhead p99 |
| --- | --- | --- | --- | --- |
| trivial (`out = in`) | 2.17 ns | 3.07 ns | +0.90 ns (+41%) | +36% |
| gain | 2.19 | 3.07 | +0.88 ns (+40%) | +27% |
| sine (tutorial oscillator) | 9.36 | 10.23 | +0.87 ns (**+9.3%**) | +9.1% |
| poly16 (16 voices) | 14.66 | 15.65 | +0.99 ns (**+6.7%**) | +6.8% |
| poly16 with `float_4` | 35.84 | 35.79 | −0.05 ns (0%) | 0% |
| sine→gain chain as a plan of 3 virtual nodes | 9.41 | 12.44 | +3.02 ns (**+32%**) | +32% |
| the same plan composed at compile time | 9.41 | 10.56 | +1.15 ns (+12%) | +12% |

The overhead is a constant **0.9–1.0 ns per frame per module**: one virtual call
and one empty-queue check. Percentages only look large for kernels that do
almost nothing. At 48 kHz, 0.9 ns is 0.004% of a 20.8 µs frame budget.

### Correctness

- Five of seven RackPy-shaped kernels produce **bit-identical** output to their
  native twins. Both plan variants differ from the fused native chain by at most
  **1 ULP** (2.4×10⁻⁷ V): the SDK's unsafe-math flag lets the compiler
  reassociate the fused version, while separate nodes round in between.
- Feedback loop in Rack: each module's output equalled its processed frame count
  **exactly** (188,800), for native and RackPy-shaped modules. This holds only
  under one-frame scheduling with Rack's one-sample cable delay; any internal
  batching would break it.

### In a real host

| Patch | Native meter | RackPy-shaped meter |
| --- | --- | --- |
| 1 sine, 48 kHz | 1.06% | 1.07% |
| 16 sines, 48 kHz | 8.64% | 8.84% |
| 64 sines, 44.1 kHz | 28.3% | 30.8% |
| 64 sines, 48 kHz | 30.0% | 31.9% |
| 64 sines, 96 kHz | 48.9% | 49.3% |
| 16 × 16-voice poly, 48 kHz | 43.2% | 42.5% |

Medians of 3 runs. Run-to-run spread is several points, so these differences are
within noise. What they do show is that **Rack's own per-module cost dominates**:
64 native sines take about 97 ns per module per frame in Rack against 9 ns of
DSP, so RackPy's 1 ns is small even beside Rack. Every run kept real time:
about 379,000 frames per module in 8 s at 48 kHz, 758,000 at 96 kHz.

### Python

| Case | ns per frame |
| --- | --- |
| Native kernel through the binding | 2.6 (p99 3.2) |
| Python kernel, GIL per frame | 318 (p99 334) |
| Python kernel, GIL per 256-frame block | 278 (p99 287) |
| Python kernel while one other Python thread is busy | 810,769 (**p99 1,126,570**) |

A single uncontended Python oscillator uses about 1.5% of a 48 kHz frame. But as
soon as any other Python code runs, the GIL hand-off (default switch interval
5 ms) makes one frame take over a millisecond, more than 50 times the whole frame
budget. **Per-frame Python is laboratory-only**, and releasing the GIL elsewhere
does not make Python safe on the audio path.

Embedding: `Py_InitializeFromConfig` took 5.1 ms and +9.0 MB RSS; importing the
binding and defining one class took 32 ms and +5.0 MB.

### UI and locks (windowed Rack)

- A Python UI callback that slept **100 ms on every frame** for 2.2 s: audio
  processed 94,815 frames against 96,069 expected (98.7%, within the fallback
  engine thread's block granularity). **A slow Python UI does not stall audio.**
- Python sleeping 50 ms inside `onReset` (exclusive engine lock): **0 frames**
  processed in 80.6 ms. Every millisecond of Python in a `ui-exclusive`
  callback is a millisecond of silence, and with a real device a dropout.

### Instrumentation

The guard reported **zero** heap, lock, write and GIL events for every
production variant. It caught every deliberately injected violation: malloc and
free (100,000 each per 100,000 frames), `new` (200,000), a mutex (100,000), a
`write` (100,000), and a per-frame Python call (102,400 GIL acquisitions, plus
307,200 mutex locks from CPython itself). An early injection went undetected
because GCC elided a `std::vector` new/delete pair at `-O3`. Injected
violations must be impossible to optimize away.

## Budgets

| Budget | Value | Basis | Status |
| --- | --- | --- | --- |
| Production audio path: Python, GIL, refcount, heap, locks, IO, logging | **Zero**, enforced by the guard in CI plus a check that kernel objects reference no `Py*` symbols | Guard validated | Frozen |
| Fixed overhead of a RackPy module vs. the equivalent C++ module | **≤ 1.5 ns per frame** absolute on this CPU class, and **≤ 10% p50 / ≤ 20% p99** for kernels of at least sine-oscillator cost | 0.87–0.99 ns; sine +9.3%, poly16 +6.7% | Frozen. For trivial kernels report nanoseconds, not ratios. |
| Graph plans (RP-21) vs. hand-fused native | **≤ 1.5 ns per frame per plan**, ≤ 1 ULP output difference | Virtual nodes +3.0 ns; static composition +1.15 ns | **Approved by the owner, 2026-10-26.** Replaces "≤10%" for plans: the remaining 12% is stores between nodes, not RackPy dispatch. RP-21's compiled tier measured +1.46 to 1.67 ns ([rp21-graph-cost.json](../../benchmarks/rp21-graph-cost.json)); the owner also approved keeping it bit-identical to the interpreter at about 0.25 ns on sine→gain, the excess. The interpreter is not held to this budget. |
| Python inside a `ui-exclusive` callback | **≤ 1 ms p99 per call**, with a UI-thread diagnostic when exceeded | Audio stops for the whole call | **Owner approval needed.** New budget. |
| Slow Python UI | Must not reduce frames processed | 98.7% with 100 ms sleeps per UI frame | Frozen as a test |
| Audio deadline misses attributable to RackPy | Zero in a 30-minute stress run | Not measurable here: no audio device | Unchanged; RP-43 on controlled hardware |
| Interactive UI response | p95 < 50 ms | Not measured | Unchanged; RP-43 |
| Lifetime stability | No live-object growth over 1,000 create/delete cycles | RP-38, in Rack with the engine running: 1,000 create/delete and 1,000 save/reload cycles returned every RackPy counter exactly, with resident memory unchanged ([validation](../../planning/rp-38-validation.md)) | Met on Linux x86_64 (`-m stress`) |
| Plugin start-up before user scripts | ≤ 250 ms and ≤ 64 MB RSS | Runtime 5 ms / 9 MB; the full bindings are still unknown. RP-08 skeleton in Rack: 12 ms, +14.2 MiB ([ADR 005](005-build-and-ci.md#results-on-the-planning-machine)) | Provisional; re-measure in RP-11 |

## Recommendation: go, with conditions

The native-path design meets its budget with room to spare. Python stays off the
audio path by construction, and the guard can prove it. Scaffolding and binding
work (RP-08 onward) can fan out, on these conditions:

1. **RP-21:** compose graph plans at compile time (or generate code) from a
   registry of native node types. A virtual call per node does not meet the
   budget.
2. **RP-14:** enforce the `ui-exclusive` Python budget and emit a diagnostic.
3. **RP-08:** run `tests/integration/test_perf_guard.py`, the guard over the
   real kernels, and the `Py*` symbol check in CI. Include SDK headers with
   `-isystem`: GCC 16 reports a false `-Warray-bounds` inside Rack's
   `Port::setChannels`.
4. **RP-43:** repeat `bin/perf-baseline` on controlled hardware with a fixed
   governor and an audio interface, and add the 30-minute deadline-miss run.
5. **Owner:** approve or adjust the two revised budgets above.

## Other findings

- Rack silently deletes a patch cable whose module widgets have no
  `PortWidget` for its ends (`RackWidget::fromJson` catches the failure from
  `CableWidget::setCable`). The benchmark plugin's first feedback runs had no
  cables for this reason.
- Rack removes cables before modules at shutdown, so a module's final output
  says nothing about the patch. Measure while connected.
