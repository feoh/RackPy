# RP-21C validation (Linux x86_64)

Date 2026-10-26, on `main` after d13e991 (part 1, 99d49ad) and after
37d3a4e (part 2), GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock` (shared with the parallel RP-22 worker).

RP-21C (`tk-rackpy-21c-compiled-graph-tier-and-native-extens-e80282`) adds
native extension node types for graphs (part 1) and the compiled tier
(part 2). Decisions are in [rp-21-slices.md](rp-21-slices.md#rp-21c-decisions).

| Command | Result |
| --- | --- |
| `uv run pytest -q -m 'not dist and not inventory'` | 247 passed |
| `uv run pytest -q tests/dsp/test_graph_native.py tests/dsp/test_graph_codegen.py tests/dsp/test_runtime.py` | 15 passed |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Extension nodes.**

- Native (`rackpy_graph_tests`): a registered node bit-identical to its
  twin over 1,000 frames of 4 channels, its reset reached through Rack's,
  a node writing 99 channels failing the plan (silenced, flagged) instead
  of being read past, and seven malformed uses refused (unknown type,
  arity, missing, repeated, unknown and unnamed arguments, non-finite) plus
  a type's own refusal at creation.
- In Rack (`tests/dsp/in_rack_libraries`): the example library's `Fold`
  graph node matches its `Fold` kernel frame for frame, `Hold` samples and
  holds, `node_types()` lists them, compile-time refusals arrive as
  `ValueError`, and two broken libraries (graph ABI 999; a node with 9
  inputs) are skipped whole, their valid kernel types included, with their
  reasons in Rack's log.

**Compiled tier.**

- `tests/dsp/test_graph_codegen.py`: the committed
  `tests/dsp/kernel_library/graphs.cpp` equals what `emit` and
  `bin/rackpy-graph emit` write today; refusals (reserved and bad
  namespaces, bad names, no graphs, extension nodes, cycles).
- In Rack: each of the five generated kernels (tutorial oscillator, every
  exact node type, sine, sine→gain, VCA) matches the plan interpreter bit
  for bit over 300 frames whose inputs switch between 0, 1, 3 and 16
  channels (both of its paths), with a control change and a sample-rate
  change; unwired fields are refused.
- Audio guard: a compiled kernel with every exact node type, alternating
  single-channel and 16-channel input, controls through the queue and
  replacement every 1,000 frames, ran 100,000 frames with zero heap, lock,
  write and GIL calls.

**Cost** ([benchmarks/rp21-graph-cost.json](../benchmarks/rp21-graph-cost.json),
P-cores 4, 6 and 8, 2 runnable processes; native baselines match RP-17's):

| Workload | Interpreter | Compiled |
| --- | --- | --- |
| mono sine (7 nodes) | +9.5 to 9.7 ns | +1.46 to 1.48 ns |
| sine→gain (9 nodes) | +11.5 to 11.7 ns | +1.65 to 1.67 ns |
| VCA (4 nodes) | +5.4 to 5.8 ns (11.0 on one noisy core) | +1.46 to 1.57 ns |
| 16 voices | −13.9 to −14.5 ns | −8.0 to −8.6 ns |

Both include RP-17's per-module kernel cost. Against ADR 004's proposed
1.5 ns per plan (owner approval pending), the compiled tier meets it for the
sine, is at it for the VCA and about 0.17 ns over for sine→gain; removing
the exactness barrier measured 1.50 ns there (owner review item, in the
slices). An earlier compiled tier without the single-channel path measured
+7.5 ns for the sine, which is why that path exists.

Only Linux x86_64 was tested.
