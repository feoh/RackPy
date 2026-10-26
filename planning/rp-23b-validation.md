# RP-23B validation (Linux x86_64)

Date 2026-10-26, on `main` after 06725f6 (RP-23A), GCC 16.2.1. Pinned inputs
as in [rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

RP-23B (`tk-rackpy-23b-one-sample-expander-message-exchange--e2a8dd`) adds
the production exchange of expander messages: graph nodes `expander_send`
and `expander_receive` in the plan interpreter, the offline reference
(`run`'s `received`/`Result.sent`, and `run_pair`) and on a laboratory
Bench, with lock-free checks on Rack's engine thread. The custom-schema C
contract for kernel libraries, the compiled tier's expander nodes, Python
snapshots and sanitizer stress were split to RP-23C. Decisions:
[rp-23-slices.md](rp-23-slices.md#rp-23b-decisions); documentation:
[graph expanders](../docs/how-to/graphs.md#expanders).

No manifest record changes status: the expander fields were bound in
RP-23A, and `producerMessage`/`consumerMessage` stay `planned` for RP-23C.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks` | build without warnings; CTest 6/6 (`graph_plans` includes the new pair tests) |
| mutation check: `readLane` without its read-once mark, `graph_plans` rebuilt and run | 1 check failed (`testRemovalAndAStoppedSenderGiveSilence`), as it should; mark restored, all checks pass |
| `uv run pytest -q tests/dsp/test_graph.py tests/dsp/test_graph_codegen.py` | 23 passed |
| `uv run pytest -m "host and not dist" tests/engine/test_expanders.py` | 1 passed (the outer test requires at least 18 inner live-UI tests, all passed), 214 s |
| `uv run pytest -m "host and not dist" tests/engine/test_expanders.py tests/dsp tests/laboratory tests/coverage/test_introspection.py tests/docs -ra` | 10 passed, 1 failed: the new snippet printed `0.0` three times, because it set an input with no cable, which Rack's `Port::setChannels` keeps at 0 channels. The snippet now drives a parameter |
| `uv run pytest -m "host and not dist" tests/docs/test_snippets.py` | 2 passed, 225 s |
| `uv run pytest -m unit` | 95 passed |
| `uv run pytest -m "docs and not host"` | 15 passed, 212 s |
| `prek run --all-files`, `bin/rackpy-dev typecheck`, `bin/planning-check`, `uv run bin/generate_bindings.py check`, `uv run bin/check_coverage.py check --without-built-package` | pass |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | first a duplicate `(expanders)=` label (removed from the how-to page), then pass with 0 warnings |

**Native** (`native/tests/test_graph.cpp`, no Rack host): a pair of RackPy
modules stepped as `Engine_stepFrame` does (every module, then the
requested flips), in both processing orders, 1,000 frames: a counter and a
16-channel ramp arrive exactly one frame later. Another schema, a one-lane
message (lane 1 does not fit, lane 0 still arrives), another plugin's
module between them, and a receiver whose left neighbour is a third module
exchange nothing. Removal (the receiver's pointer cleared, as Rack's
`removeModule` does) gives silence at once; a sender that stops is heard
once more, then as silence; a new receiver in the same place is found at
the next frame. Bad plans (side 2, lane 64 or 0.5, a lane sent or received
twice, an input on a receive) are refused, and a plan that only sends is
valid.

**Inside actual Rack** (`tests/engine/expanders/in_rack/test_exchange.py`,
live UI, test hooks; Rack's engine stepped by the test with a RackPy master
module and `Engine.step_block`):

- A sender and a receiver that echoes back, at 44,100, 48,000 and 96,000 Hz
  with `settings.thread_count` 1, 2 and 4 (9 runs): 48 single frames equal
  the offline reference's `run_pair` exactly, so every message arrives one
  frame after it was sent, both ways; then one block of 256 frames ends
  with the same one-frame relation.
- A laboratory Bench gives the same 48 frames as the reference.
- Reordering (a third module moves in on the receiver's left) delivers what
  was already flipped, then the new neighbour's values one frame later;
  removing the neighbour through Rack's engine clears the receiver's
  expander and gives silence at the next frame; a receiver whose messages
  have another schema receives nothing, while its own echo still reaches
  the sender.

Not covered here (RP-23C): kernel libraries and the compiled tier,
snapshots for Python while Rack's engine runs, and sanitizer stress. The
hook that places modules writes expander IDs as `RackWidget` does, so module
widgets dragged in the real UI were not exercised. Only Linux x86_64 was
tested; macOS and Windows were not built.

After rebasing onto `7d2c543` (RP-22E): `bin/rackpy-dev build --werror
--test-hooks` (CTest 6/6), `uv run pytest -m unit` (95 passed), `prek run
--all-files`, `generate_bindings.py check` and `check_coverage.py check
--without-built-package` (implemented 2942, planned 2, unreviewed 1) pass,
and `uv run pytest -m "host and not dist" tests/engine/test_expanders.py
tests/coverage/test_introspection.py tests/docs/test_snippets.py
tests/laboratory` gives 8 passed (477 s).
