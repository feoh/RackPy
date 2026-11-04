# Performance and real-time safety results

This page reports evidence and its limits. It does not turn uncontrolled
measurements into a performance claim.

## Linux x86_64 safety and lifecycle run (2026-11-04)

The evidence record is
[`release-evidence/benchmarks/linux-rp43-safety.json`](../release-evidence/benchmarks/linux-rp43-safety.json).
It was collected from RackPy commit
`7444cc40f3fcacc6ca0d97fab5ad36b74513205b` on an Intel Core i9-14900K running
CachyOS Linux 7.3.1. The CPU governor was `powersave`; although audio capture
devices were detected, the test did not run a physical audio-output loopback
or a 30-minute deadline-miss workload. **This is not controlled-hardware
performance evidence.**

The following checks passed locally:

- The production-shaped native audio-path guard observed zero heap, mutex,
  write, and GIL events. Deliberately injected allocation, mutex and write
  violations were detected. Audio and MIDI output matched Rack references;
  sanitizer-backed audio and expander stress checks passed.
- Rack-host stress exercised concurrent engine operations and hostile callbacks.
  The audio-path guard again saw zero forbidden events. One thousand
  create/delete and one thousand save/reload cycles retained stable RackPy
  counters. RSS was unchanged at the 1,000th create/delete cycle; save/reload
  RSS increased from 228,405,248 bytes at cycle 300 to 232,251,392 bytes at
  cycle 1,000, while the reported object counters remained stable.
- Slow callbacks outside Rack's exclusive engine lock did not reduce the
  measured frame rate in this run. Slow callbacks inside the exclusive lock
  produced the expected stall and diagnostic; this is an exposed limitation,
  not evidence that callbacks holding that lock are safe for uninterrupted
  audio.

Exact commands and outcomes are retained in the JSON record. These are safety
and lifecycle checks on one Linux host, not a 30-minute live-device test, an
A/B performance benchmark, or proof of lock-free behavior for the entire Rack
host path. The measured frame rates and RSS are diagnostic only.

## Candidate installation evidence

The GitHub Actions run
[41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030) passed
release-candidate installation/run and in-candidate manifest checks on Linux
x86_64, macOS x86_64 and arm64, and Windows x86_64. Platform native tests and
Rack-host tests also passed. Those jobs establish the tested candidate's
installation and smoke-test coverage; they are not performance benchmarks or
real-audio-device deadline evidence. The run's optional documentation external
link check failed because GitHub returned HTTP 429 for an upstream license URL.

## Still required for a performance claim

- Repeat the frozen A/B suite on controlled hardware with fixed CPU policy and
  equivalent native modules.
- Run a 30-minute physical audio-device deadline-miss test and measure UI
  latency under the documented workloads.
- Publish the raw measurements, conditions and uncertainty; reproduce the
  results independently. Dynamic audio-path instrumentation is Linux-only
  today; platform gaps remain explicit.

See the [performance and safety contract](performance.md) and
[ADR 004](decisions/004-performance-budgets.md) for the budgets and full
methodology. No performance claim is made until those gates are met.
