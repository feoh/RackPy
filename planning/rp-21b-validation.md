# RP-21B validation (Linux x86_64)

Date 2026-10-26, on `main` after 9b1e096, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock` (shared with the parallel RP-22 worker).

RP-21B (`tk-rackpy-21b-native-graph-plan-interpreter-cef2f3`) adds the
native plan interpreter: `native/dsp/graph/nodes.hpp` (the node types, shared
with RP-21C), `native/dsp/graph/plan.{hpp,cpp}` (checking a plan, its
`rackpy.Graph` kernel type, the interpreter), `Graph.compile()`, and
`_testing.module_process` (steps a Python-owned module frame by frame).
Decisions are in [rp-21-slices.md](rp-21-slices.md).

| Command | Result |
| --- | --- |
| `uv run pytest -q -m 'not dist and not inventory'` | 242 passed |
| `uv run pytest -q tests/dsp/test_graph_native.py tests/dsp/test_runtime.py` | 11 passed; the in-Rack kernel suite now asserts every file reported, `test_graph.py` included |
| `uv run pytest -q tests/docs/test_snippets.py` | 5 passed; `graph_kernel.py` runs in Rack and prints its expected output |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Native, against direct C++ twins** (`rackpy_graph_tests`, the SDK's flags;
each twin keeps intermediate values where the plan has separate nodes, so
`-funsafe-math-optimizations` cannot reassociate across them):

- Rack's tutorial oscillator and its blinking light, for 0, 1, 4 and 16
  pitch channels over 100,000 frames; a VCA with 1 to 16 channels changing
  every frame over 100,000 frames; 16 voices with a control changed through
  the slot's queue mid-run; a one-pole lowpass and a counter through
  delays; Rack's `SchmittTrigger` and `PulseGenerator` on 3 channels. All
  bit-identical. A one-ULP mutation of two twins was caught, then reverted.
- Fields and wiring checks, reset (state starts over, controls kept),
  replacement through the slot keeping the plan's type alive until the main
  thread reclaims it, and 21 malformed plans refused with the node at fault.

**Audio-path guard** (`rackpy_kernel_guard`, `LD_PRELOAD=audio_guard.so`):
a 16-channel plan with every node type, controls set through the queue
every 7 frames and the plan replaced every 1,000 frames, ran 100,000 frames
with zero heap, lock, write and GIL calls; the injected allocation was still
caught. `plan.cpp.o` references no CPython symbols.

**Inside actual Rack** (`tests/dsp/in_rack/test_graph.py`, hooks build):

- A plan with every exact node type, stepped 600 frames on a Python-owned
  module with random inputs of 0 to 16 channels, a control change and a
  sample-rate change, matches the offline reference exactly, channel counts
  included. The tutorial oscillator stays within 1e-3 V of the reference
  over 2,000 frames (libm's float `sin` and `pow` against Python's,
  rounded), and its light matches exactly.
- Wiring refused by `set_kernel`, cycles refused before compiling,
  malformed plan dicts refused natively, a plan run by Rack's own engine
  with its control changed through the queue, and 300 replacements under
  load followed by teardown.

**Cost** ([benchmarks/rp21-graph-cost.json](../benchmarks/rp21-graph-cost.json),
P-cores 4, 6 and 8, while the parallel worker loaded other cores; native
baselines match RP-17's): the interpreter adds about 9.5 ns per frame to a
mono sine (7 nodes), 11.5 ns to sine→gain and 6.5 ns to a VCA, about 1.3 to
1.6 ns per node that runs each frame, so mono plans do **not** meet ADR 004's
proposed 1.5 ns per plan (owner approval pending). That is RP-21C's job.
A 16-voice plan is 12.5 ns per frame faster than the hand-written loop.

Only Linux x86_64 was tested.
