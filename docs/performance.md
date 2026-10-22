# Performance and safety contract

“Reasonably performant” needs an honest baseline and measurement conditions.
The following are **proposed release budgets**, not results. RP-07 must measure
feasibility and freeze justified budgets before implementation fans out.

## Architectural invariants

Production `process` and `processBypass` paths must contain no Python calls,
GIL acquisition, Python reference-count transitions, heap allocation/free,
blocking locks, IO, network, GUI calls, or synchronous logging. Expander message
exchange remains native and preserves Rack's one-frame latency.

UI → engine updates have bounded queues and a documented overflow policy.
State/graph replacement reclaims memory off the audio thread. Atomics must be
proven lock-free for the target where used. A slow UI must not block processing.

The non-real-time Python callback laboratory is measured separately and never
presented as production performance.

## Proposed measurable budgets

**RP-07 measured these on the planning machine and froze working budgets in
[ADR 004](decisions/004-performance-budgets.md)** (raw data:
[benchmarks/baseline.json](../benchmarks/baseline.json)). Two revisions await
owner approval there. The table below is the original candidate list.

| Measurement | Initial candidate |
| --- | --- |
| Native-path overhead versus equivalent C++ module | ≤10% median and ≤20% p99 processing cost for representative nontrivial kernels |
| Audio deadline misses attributable to RackPy | Zero in a 30-minute fixed-hardware stress run |
| Audio-thread Python/GIL/heap events in production mode | Zero, instrumented |
| Interactive UI response | p95 <50 ms for sample control changes |
| Lifetime stability | No live-object growth after 1,000 create/delete cycles beyond documented bounded caches |

Report trivial-kernel absolute nanoseconds as well as ratios: empty callbacks
make overhead percentages misleading. Timing in shared hosted CI is diagnostic;
controlled hardware results gate performance claims. These budgets may be revised
only with recorded evidence and owner approval.

## Workloads and evidence

Use the same compiler flags, sample rate, channel count and signal stream in
native and RackPy modules. Cover 44.1/48/96 kHz, mono and 16-channel polyphony,
1/16/64 modules, bypass, sample-rate changes, UI load, expander pairs and
multithreaded engine operation. Verify the pinned SDK's actual channel limit.

Warm up both runs; randomize A/B order; record repetitions, distributions,
hardware, power policy, OS, compiler, SDK/Python versions and raw data.
Measure native-call overhead, array copies, callback cost, CPU, tail latency,
startup/import time and memory separately. Test feedback graphs so an illegal
block-processing optimization cannot pass.

Use native allocation/GIL hooks, sanitizer jobs where compatible, stale-handle
tests, queue-overflow tests and a forced-slow-Python-UI test. Never infer
real-time safety just from low mean CPU usage.
