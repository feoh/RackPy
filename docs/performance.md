# Performance and safety contract

“Reasonably performant” needs an honest baseline and measurement conditions.
The following are **proposed release budgets**, not results. RP-07 must measure
feasibility and freeze justified budgets before implementation fans out.

## Architectural invariants

RackPy's production `process`, `processBypass`, and audio-driver callback paths
must contain no Python calls, GIL acquisition, Python reference-count
transitions, heap allocation/free, IO, network, GUI calls, or synchronous
logging. RackPy must not introduce additional blocking locks on these paths.
This does not prohibit synchronization already performed by Rack's host or
driver when using the supported public API. For example, Rack 2.6.6 holds
`audio::Device::processMutex` while dispatching port callbacks, and
`Engine::stepBlock` takes engine locks. Document and test RackPy's own path
separately; do not characterize the complete Rack host path as lock-free.
Expander message exchange remains native and preserves Rack's one-frame latency.

UI → engine updates have bounded queues and a documented overflow policy.
State/graph replacement reclaims memory off the audio thread. Atomics must be
proven lock-free for the target where used. A slow UI must not block processing.

The non-real-time Python callback laboratory is measured separately and never
presented as production performance.

## Proposed measurable budgets

**RP-07 measured these on the planning machine and froze working budgets in
[ADR 004](decisions/004-performance-budgets.md)** (raw data:
[benchmarks/baseline.json](../benchmarks/baseline.json)). The owner approved
its graph-plan budget on 2026-10-26; one revision still awaits approval there. The table below is the original candidate list.

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

## Laboratory callback cost (RP-22D)

**Not production real-time performance.** This benchmark runs the implemented
callback laboratory, offline on Rack's main thread at plugin startup, with
the GIL already held. It measures no audio device, worker scheduling, GIL
contention, per-frame tail latency or deadline misses. In particular, the
percentiles of repeated 20,000-frame run averages would not be callback tail
latencies. ADR 004's contended-GIL experiment remains relevant; this test does
not supersede it or satisfy a production budget.

Raw data: [`benchmarks/rp22-laboratory-cost.json`](../benchmarks/rp22-laboratory-cost.json).
The file contains every elapsed time in randomized execution order, all
per-frame run averages, the exact native baseline, machine/kernel/governor/
energy policy/affinity/load, bundled Python version, compile command and
benchmark/native source hashes. This is shared desktop diagnostic evidence,
not controlled hardware. Linux x86_64 only; no macOS or Windows measurement.

Measured on Intel Core i9-14900K, Linux `7.3.0-4-cachyos`, GCC 16.2.1,
bundled CPython 3.14.9, Rack Free 2.6.6. Power policy was left as found:
`powersave`, `balance_performance`, turbo enabled, affinity CPUs 0–31, load
average 3.34 at collection. Results are **nanoseconds per frame averaged over
each run**, not individual callback tail latencies:

| Variant | Minimum run average | Median run average | Maximum run average |
| --- | ---: | ---: | ---: |
| Native empty RackPy Module on Bench (baseline) | 3.052 | 3.055 | 3.566 |
| Laboratory module, Rack base process/step | 3.958 | 4.306 | 4.577 |
| Empty Python process override | 133.161 | 135.813 | 138.602 |
| Python process calling super().process | 265.305 | 266.816 | 269.486 |

Four otherwise empty one-module benches at 48 kHz, no parameters, channels,
cables or kernels: an ordinary native RackPy Module (the baseline), a
laboratory module using Rack's base `process`/`step`, an empty Python override,
and a Python override calling `super().process`. Each bench warms up for 2,000
frames; 21 repetitions of 20,000 frames each use seed 22 to shuffle A/B/C/D
order. One `Bench.step(20000)` call per run drives the native host loop, still
calling the virtual once per sample; this measures Bench scheduling,
trampoline/argument conversion and callback body together. It is not an
isolated C++ virtual-call benchmark and not Rack's real engine.

Reproduce just the benchmark (fresh production build through the standard
host harness; no device is opened):

```bash
mkdir -p build/validation
set -o pipefail
RACKPY_LABORATORY_COST_OUT=benchmarks/rp22-laboratory-cost.json \
  CMAKE_BUILD_PARALLEL_LEVEL=3 flock /tmp/rackpy-host-tests.lock \
  uv run pytest tests/laboratory/test_callback_cost.py -ra \
  --basetemp=build/validation/cost 2>&1 | tee build/validation/cost.log
```

The retained run's exact command is in the JSON (the closing full host suite
also runs this benchmark). See [RP-22D validation](../planning/rp-22d-validation.md)
for actual command results, failures and remaining gates, and
[Callbacks and the laboratory](explanation/callbacks.md) for the restrictions.
