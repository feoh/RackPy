# RP-23C validation (Linux x86_64)

Date 2026-10-26, on `main` after 993a12d (RP-23B), GCC 16.2.1. Pinned inputs
as in [rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-23C (`tk-rackpy-23c-native-expander-schema-contract-compi-a3a9d8`) adds
the public C++ contract by which kernel libraries exchange expander
messages under schemas of their own (`rackpy/expanders.hpp`, RackPy's
`Services` given to a library defining `RACKPY_EXPANDERS`), the compiled
tier's expander nodes through it, seqlock snapshots that Python reads while
Rack's engine runs, and sanitizer stress. `producerMessage` and
`consumerMessage` move from `planned` to `implemented`; with them no
`rack::engine::Module` record is left open (coverage: implemented 2944,
unreviewed 1, excluded_internal 111, absent 2). Decisions:
[rp-23-slices.md](rp-23-slices.md#rp-23c-decisions); documentation:
[from C++](../docs/how-to/dsp.md#exchange-expander-messages-from-c),
[snapshots](../docs/reference/engine-module.md#expanders).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks` | first failed: GCC refuses `atomic_thread_fence` under `-fsanitize=thread` as an error; the snapshot now uses release stores and acquire loads, and the TSan target passes `-Wno-tsan` only for the diagnostics ring's existing fences. Then built without warnings; CTest 6/6 |
| `uv run pytest -m "native and not host" tests/dsp tests/engine` | 13 passed (262 s): graph plans, kernels, the audio-path guard (`expanders` run: no allocation, lock, write or GIL call while interpreted and compiled pairs exchange with snapshots on), and the stress runs below |
| `rackpy_expander_stress_asan 1 1500` | 48,668 frames, 1,500 changes, 19,723,884 snapshot reads; all checks passed, no ASan/UBSan/LeakSanitizer report |
| `rackpy_expander_stress_asan 4 3000` | 98,199 frames, 3,000 changes, 43,305,858 snapshot reads; all checks passed |
| `rackpy_expander_stress_tsan 4 1500` | 48,668 frames, 1,500 changes, 6,447,689 snapshot reads; all checks passed, no ThreadSanitizer report |
| mutation: relink keeps a deleted neighbour's pointer | ASan: heap-use-after-free in `engine::resolve`, as it should; reverted |
| mutation: snapshot writer without its odd sequence mark | first undetected (a one-value lane cannot tear visibly), so the stress now sends 16 equal channels; then `inconsistent.load() == 0` failed, as it should; reverted |
| `uv run pytest -m unit` | first 2 failed: the codegen test still expected expander nodes to be refused (now a test that they compose through the services), and `test_every_module_gap_retains_a_responsible_task` asserted that some Module gap exists (none does now; the check of any future gap stays); then 96 passed |
| `uv run pytest -m "docs and not host"` | 15 passed |
| `prek run --all-files`, `bin/rackpy-dev typecheck`, `bin/planning-check`, `uv run bin/generate_bindings.py check`, `uv run bin/check_coverage.py check --without-built-package` | pass |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |
| `uv run pytest -m "host and not dist" tests/engine/test_expanders.py tests/dsp tests/coverage/test_introspection.py tests/docs/test_snippets.py tests/laboratory -ra` | 10 passed, 1 failed (471 s). The expander suite (at least 20 inner tests) and introspection passed; in the libraries suite, 15 inner tests passed and the new `levels` test failed only at its last line: the example kernels did not refuse an unwired port, as generated kernels do. They now throw `levels.Send: input must be wired` |
| `uv run pytest -m "host and not dist" tests/dsp/test_runtime.py -ra` | 2 passed (the libraries test bundle: all 16 inner tests passed, and the skipped-library log lines include the two new expander refusals), 220 s |

**Inside actual Rack:**

- `tests/dsp/in_rack_libraries/test_libraries.py` (the libraries test
  bundle): the compiled `graphs.Sender | graphs.Echo` and the interpreted
  pair run side by side in Rack's own engine (stepped by the test): 64
  frames equal to each other and to `run_pair`, with equal snapshots; the
  `levels` library's `Send | Receive` exchange under `"levels.Levels"`
  (one frame for the cable, one for the expander), and nothing reaches a
  neighbour with `"rackpy.graph"` messages; libraries with a wrong
  `EXPANDER_ABI` or half of `RACKPY_EXPANDERS` are skipped with the reason.
- `tests/engine/expanders/in_rack/test_exchange.py`: Python reads 200
  snapshots while Rack's own engine thread runs a pair; frames only
  increase and every snapshot's counter is the frame plus one constant
  offset (never torn); turning snapshots off stops them; another plugin's
  module has none.

Not covered: real hardware audio threads (the tests have no audio device),
macOS and Windows (not built), and a foreign plugin's own expander layout,
which RackPy deliberately leaves to native code that checks that plugin's
model.
