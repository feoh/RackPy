# RP-22B validation (Linux x86_64)

Date 2026-10-26, on `main` after 765adcb, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-22B (`tk-rackpy-22b-synchronous-laboratory-module-events--23893f`) adds
synchronous laboratory overrides of a module's events and JSON virtuals and
the Bench's remaining engine operations: 7 records implemented
(`onSampleRateChange` both overloads, `onSetMaster`, `onUnsetMaster`,
`onExpanderChange`, `ExpanderChangeEvent`, its `side`), 18 annotated with
their laboratory path. Design: [rp-22-slices.md](rp-22-slices.md);
reference: [laboratory events and JSON](../docs/reference/engine-module.md#laboratory-events-and-json).

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror` and `--test-hooks --build-dir build/hooks` | build; CTest 5/5 |
| `uv run pytest -q tests/coverage tests/engine tests/plugin tests/docs tests/unit tests/native tests/dsp/test_runtime.py tests/laboratory` | 126 passed, 2 failed: the docs build, with 17 nitpick warnings from the new `rackpy.laboratory` annotations and `:meth:` links (fixed: a reviewed `nitpick_ignore_regex` entry for Rack's nested event records, plain literals for inherited methods) |
| `uv run pytest -q tests/docs` after the fix | 17 passed |
| `uv run pytest -q tests/laboratory tests/coverage` (final) | 51 passed, including the introspection check of every new name in the built package |
| `prek run --all-files`, `uv run bin/generate_bindings.py check`, `uv run bin/check_coverage.py check --without-built-package`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html`, `bin/planning-check` | pass, 0 warnings |

**Inside actual Rack** (`tests/laboratory/in_rack/`):

- **Bench events against Rack's engine** (live UI, test hooks): two RackPy
  modules with a parameter went through the same edits in Rack's engine
  (test-hook adoption, `Engine.set_master_module`, cable, bypass, reset,
  randomize, `prepareSave`, sample rate, expander IDs set through Rack's
  `moduleFromJson` and followed by `step_block(1)`, unsetting the master,
  disconnecting, deleting) and on a Bench. Each module's native snapshot
  (`lifecycle_events()`) was identical, all twelve event kinds included. A
  laboratory class overriding every event and calling `super()` received
  exactly those events in Python, in the same order.
- **Laboratory semantics**: Rack's dispatch order on a bench (add, cables,
  bypass, sample rate, master, reset, randomize, save, expanders, removal
  with master and expander clearing); overrides replacing Rack's behaviour
  unless they call `super()` (no snapshot, and an `on_reset` that resets no
  parameters); event errors reaching the bench caller after the other
  modules got their events, not contained; JSON overrides answering
  synchronously, with and without a model; Rack's own `toJson` and
  `fromJson` calling the Python `params_*` and `data_*` overrides (live
  context, a laboratory module named with the hooks' `RuntimeProbe` model);
  `ContextError` for parameter JSON, reset and randomize without Rack's
  context (start-up run); normal-mode refusal of the driver and engine
  events in `set_event_handlers`.

The first in-Rack run failed one 22A test because the no-override `process`
had been changed to the RackPy kernel path instead of Rack's `process`
(which calls `step()`); restored. A second failure was `Module.to_json`
requiring Rack's context for a laboratory module without parameters, which
does not need it; relaxed for that case only. The outer wrapper first
rejected each run's intentionally skipped test; it now requires exactly that
one skip.

Only Linux x86_64 was tested; macOS and Windows were not built.
