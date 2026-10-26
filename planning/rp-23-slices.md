# RP-23 bounded implementation slices

RP-23 (`tk-rackpy-23-implement-safe-expander-access-and-one-619c26`) maps
Rack's expanders: each module's `leftExpander` and `rightExpander`, whose
`void*` double buffers carry messages between adjacent modules with one
sample of latency. These child contracts supplement, not replace,
[RP-23 in TASKS.md](TASKS.md); live Witan status is authoritative.
Decisions follow the owner's standing instruction to take the recommended
option, and are recorded here.

## Decisions

- **Rack's contract.** An expander's `moduleId` and `module` are written by
  Rack on the main thread (`RackWidget`, patch loading, the engine's
  `setExpanderModule`, which is PRIVATE). A module allocates both message
  buffers itself, and Rack's engine swaps them at the end of a frame in
  which the flip was requested. A module must check its neighbour's model
  before reinterpreting the neighbour's `void*`.
- **No raw pointers.** Python never sees a `void*` as an address or a
  memory view of unknown size. RackPy allocates **message buffers** with a
  schema (a name and a count of float32 values), on the main thread, and
  recognizes them by looking their address up in its own registry, never by
  reading the memory. Any other pointer, a foreign plugin's, is an opaque
  handle: truth and identity only. Unresolved arbitrary-pointer semantics
  stay visible in the coverage records.
- **No racy reads.** Rack's engine thread reads and writes the buffers and
  the flip flag while it runs a module, and Rack has no public way to pause
  it. Python therefore assigns buffers, requests flips and reads or writes
  message contents only while Rack's engine is not running the module
  (Python-owned modules, construction, the RP-22 laboratory), and is refused
  otherwise. While the engine runs a module, Python reads **snapshots** that
  native code publishes (RP-23B).
- **Production exchange is native.** Native kernels and graph nodes produce
  and consume messages on the engine thread. Before reinterpreting a
  neighbour's buffer they check that the neighbour is a RackPy module
  advertising the same schema; otherwise they read silence and flag it.

## Slices

**RP-23A — the raw expander mapping and message buffers.**
`Module.left_expander`, `right_expander`, `get_left_expander()`,
`get_right_expander()` and `get_expander(side)` return `Expander` views tied
to the module's lifetime. `module_id` and `module` (a borrowed view of the
neighbour, or `None`) are read on the main thread. `producer_message` and
`consumer_message` give a RackPy `ExpanderMessage` (schema, size, contents),
an opaque `ForeignMessage`, or `None`; assigning them, `message_flip_requested`
and `request_message_flip()` follow the rule above, and assigned buffers
belong to the module from then on. Tests inside Rack; docs.

**RP-23B — one-sample production exchange.** Graph nodes that send to and
receive from an expander, in both tiers and the offline reference; neighbour
and schema checks, and invalidation on removal, reordering and mismatch;
snapshots for Python while the engine runs; a Rack pair fixture measuring
exactly one frame of latency across sample rates and engine thread counts;
hot removal, reordering, mismatch and repeated flips under sanitizers; the
interoperability and extension contract for custom native schemas.
