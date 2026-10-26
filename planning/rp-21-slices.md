# RP-21 bounded implementation slices

RP-21 (`tk-rackpy-21-compile-python-authored-dsp-graphs-int-c547be`)
compiles Python-authored DSP graphs into native plans that run on Rack's
engine thread with no Python. These child contracts supplement, not
replace, [RP-21 in TASKS.md](TASKS.md); live Witan status is authoritative.
Decisions follow the owner's standing instruction to take the recommended
option, and are recorded here.

## Decisions

- **One node registry, two execution tiers, one semantics.** A graph is
  built in Python from typed nodes, validated, and lowered to a plan
  description: nodes in execution order, each reading earlier nodes' signals
  by index, with constants and named controls. Two tiers run that same
  description:
  - **Plan interpreter (21B).** The description becomes an immutable native
    plan on the main thread, with all state and signal storage preallocated,
    and runs as an RP-17 `Kernel`. It needs no compiler at runtime.
    Publication, the control queue, frame-boundary replacement and
    main-thread reclamation are RP-17A's `KernelSlot`, unchanged.
  - **Compiled tier (21C).** Code generation emits a kernel library (RP-17B)
    that composes the same native node types statically. This is ADR 004's
    condition 1: a virtual call per node does not meet the proposed
    ≤ 1.5 ns per plan budget, which is still awaiting owner approval. The
    interpreter's cost is measured and reported against that budget, not
    assumed.
- **Signals.** Every edge carries a polyphonic signal of 0 to 16 channels,
  computed per frame, as Rack's cables do. Inputs carry their cable's
  channels. A mono operand broadcasts across the other operand's channels,
  and channels missing from a shorter poly operand read 0 V. Sources that
  need a channel (oscillators) use at least one. Gates and triggers are
  signals of 0 and 1.
- **Order and feedback.** Nodes run once per frame, in a topological order
  of the graph. A cycle must pass through an explicit one-sample `delay`;
  any other cycle is rejected with the cycle's path in the message. No
  latency is ever added implicitly, and nothing is batched across frames or
  modules.
- **Boundaries.** A graph reads and writes only its own module's ports,
  params and lights. Expanders are RP-23's; a graph cannot reach them.
- **What is not compiled.** Arbitrary Python functions, lambdas and
  callbacks are not translated into native code. A graph is built only from
  registered native node types; native extension node types come from kernel
  libraries (21C).
- **The offline reference** (`rackpy.dsp.graph_reference`) evaluates a
  graph's plan description in Python, for tests and teaching, rounding to
  float32 after each operation. It is never the production path and says so.

## Slices

**RP-21A — graph model, validation and the offline reference.**
Python only: `rackpy.dsp.graph` (Graph, typed node handles, operator
overloading, connection and cycle validation, lowering to the plan
description) and `rackpy.dsp.graph_reference`. Tests in
`tests/dsp/test_graph.py`; how-to documentation of the model, with
executable examples that use the offline reference only.

**RP-21B — the native plan interpreter.**
`native/dsp/graph/`: a header of native node types (Rack's own
`dsp::SchmittTrigger` and `dsp::PulseGenerator` among them), the plan built
from the description, and a `Kernel` that runs it. `Graph.compile(module)`
checks wiring against the module's configuration and returns a kernel for
`Module.set_kernel`, with controls through `kernel.set`. Native twins and
in-Rack tests cover: sine with a blinking light, VCA, and 16-channel poly,
each against equivalent direct C++; feedback through `delay`; control
changes mid-run; plan replacement while running and during deletion; and
bad cycles. An audio-guard run checks a 16-channel plan, and a cost
benchmark is recorded.

**RP-21C — the compiled tier and extension nodes.**
Code generation from a graph to a kernel library composing the same node
types statically (`bin/rackpy-graph`, cyclopts), checked equal to the
interpreter and benchmarked against ADR 004. Native extension node types
registered by kernel libraries, through a public header and an optional
library entry point.
