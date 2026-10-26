# RP-23A validation (Linux x86_64)

Date 2026-10-26, on `main` after 9baa5b0, GCC 16.2.1. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-23A (`tk-rackpy-23a-raw-expander-mapping-and-rackpy-messa-6b626e`) maps
Rack's `Module::Expander` and the module's expander accessors: 12 records,
10 `implemented` and 2 (`producerMessage`, `consumerMessage`) bound but
left `planned` for RP-23B. Decisions: [rp-23-slices.md](rp-23-slices.md);
reference: [expanders](../docs/reference/engine-module.md#expanders).

The work was started by session 01a1787a, stopped by a system restart as an
unvalidated checkpoint (`origin/rp23a-wip` @ bebb400), and finished here.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks` | build without warnings; CTest 6/6 |
| `uv run pytest -m "host and not dist" tests/engine/test_expanders.py` (first run of the checkpoint) | 1 failed: 3 of 7 inner tests. A Python-owned neighbour or owner came back as an `EngineModule` (fixed with `liveModuleObject`), and a test looked for message objects whose Python wrappers it had dropped (fixture now keeps them) |
| the same, after the fixes | 1 passed (7 inner live-UI tests passed), 257 s |
| `uv run pytest -m "host and not dist" tests/engine tests/laboratory tests/coverage/test_introspection.py tests/docs/test_snippets.py -ra` | 14 passed, 1 failed: the introspection check did not import `rackpy.engine.expanders`, so the new facade names were "not in the built package"; added to `rackpy._introspect.MODULES` |
| `uv run pytest -m "host and not dist" tests/coverage/test_introspection.py` | 3 passed (the manifest checked against the built package inside Rack), 220 s |
| `uv run pytest -m unit` | first 1 failed (`test_every_module_gap_retains_a_responsible_task`: the two planned records must start with `Responsible task: RP-`; notes reordered), then 91 passed |
| `uv run pytest -m "docs and not host"` | 15 passed |
| `prek run --all-files`, `bin/rackpy-dev typecheck`, `bin/planning-check`, `uv run bin/generate_bindings.py check`, `uv run bin/check_coverage.py check --without-built-package` | pass (coverage: absent 2, excluded_internal 111, implemented 2941, planned 3, unreviewed 1) |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Inside actual Rack** (`tests/engine/expanders/in_rack/test_expanders.py`,
live UI, test hooks):

- **Views**: `left_expander`/`right_expander`, the three getters and
  `get_expander(side)` (0 left, any other `uint8_t` right, 256 refused) on a
  RackPy module; a view reads the module's own fields (flag per side,
  `module_id` write-through, -2 refused) and is stale after `close()`.
- **Messages**: schema and size limits, indexing, `tolist`, `write`; a
  module keeps its buffers alive after the last Python reference goes, a
  buffer is in one slot at a time, both slots of a side need the same
  schema and size, raw values are refused, and a cleared buffer is free for
  another module; after the module closes the buffer has no owner.
- **Rack's engine runs the module** (test hooks adopt two RackPy modules
  into Rack's engine): `module` is found by `module_id` through Rack's
  engine and is the neighbour's own handle; slots, flag, flip request and
  `module_id` writes raise `OwnershipError`, as do the contents of the
  buffers that module holds.
- **Another plugin's module** (the RuntimeProbe test module): its expander
  views read `module_id`/`module`, refuse its slots while Rack's engine
  runs it, and are stale once Rack removes it.
- **Laboratory Bench**: `set_expander` then `step` sets `module`; a flip
  happens only when requested, at the end of the frame, and clears the
  flag; removing the neighbour clears `module` and `module_id`.
- The documentation snippet `docs/snippets/expanders.py` ran inside Rack
  (`tests/docs/test_snippets.py`).

Not covered here (RP-23B): native production exchange on Rack's engine
thread, snapshots while it runs, the measured one-frame latency across
sample rates and thread counts, sanitizer runs of hot removal, reordering,
mismatch and repeated flips, and the native-schema interoperability and
extension contract. Only Linux x86_64 was tested; macOS and Windows were
not built.
