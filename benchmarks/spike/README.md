# RP-07 benchmark spike

**Spike code, not RackPy.** It measures the proposed production design against
plain C++ modules and proves the audio-path instrumentation. The results and
budgets are in [ADR 004](../../docs/decisions/004-performance-budgets.md); raw
data is in [../baseline.json](../baseline.json).

| File | What it is |
| --- | --- |
| `src/dsp.hpp` | Shared DSP (sine, 16-voice poly, SIMD poly, gain) and a lock-free SPSC ring |
| `src/bench.cpp` | Standalone harness linking libRack. `time` (randomized A/B), `equivalence` (bit-exact), `guard` (needs `LD_PRELOAD`) |
| `../../native/testing/audio_guard.c` | `LD_PRELOAD` library (promoted to RackPy test instrumentation in RP-17A) counting heap, mutex, `write` and GIL calls on marked audio paths |
| `src/bench_lab.cpp` | Python laboratory cost, contention, start-up time and memory |
| `src/host_bench.cpp`, `plugin.json` | Rack plugin with native and RackPy-shaped modules, for real-host meter and feedback runs |

```bash
bin/perf-baseline run      # everything; rewrites benchmarks/baseline.json (about 7 minutes)
uv run pytest tests/integration/test_perf_guard.py   # guard and equivalence checks
```

The slow-UI and exclusive-stall checks use `spikes/lifecycle` and need
`xvfb-run`. Results depend on the machine: `baseline.json` records the CPU,
governor and pinning, and says whether the hardware was controlled.
