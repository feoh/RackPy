# Describe DSP as a graph

A **graph** describes a module's per-sample processing in Python, built from
native node types: ports, parameters and controls in, arithmetic,
oscillators and Rack's own triggers in between, outputs and lights out.
Python only describes it; nothing in a graph runs Python per sample.

**Implemented (RP-21A, RP-21B, RP-21C; expander nodes RP-23B and
RP-23C), Linux x86_64 inside Rack Free 2.6.6.** {py:mod}`rackpy.dsp.graph` builds, validates and lowers graphs.
{py:meth}`~rackpy.dsp.graph.Graph.compile` turns one into a native
[kernel](dsp.md) that the **plan interpreter** runs on Rack's engine
thread, straight from Python. The **compiled tier**
({py:mod}`rackpy.dsp.graph_codegen`, `bin/rackpy-graph`) turns graphs into
C++ for a kernel library, for the lowest cost. Both compute exactly the same
results. {py:mod}`rackpy.dsp.graph_reference` evaluates graphs offline, in
Python, for tests and teaching.

Building, checking and the offline reference work outside Rack;
`compile()` needs Rack.

## Run a graph in a module

```{rackpy-example} graph_kernel.py
:caption: Rack's tutorial oscillator as a graph, run natively
```

`compile()` checks the plan again natively and allocates everything the plan
needs, on the main thread. The kernel's fields are the ports, params and
lights the graph uses (`input[0]`, `light[0]`, ...), which `set_kernel`
checks against the module's configuration, followed by its controls, which
`Kernel.set` changes through RP-17's bounded queue at a frame boundary.
Replacing a module's kernel, plan or not, happens at a frame boundary, and
the old one is freed on the main thread. Rack's reset (Initialize) starts
the nodes' state over (phases, triggers, pulses, delays) and keeps the
controls Python set.

**Cost.** The interpreter runs a plan's nodes in order through one `switch`,
with no virtual call per node, and writes constants and controls in
advance. That still costs more than the same DSP fused by hand in C++: about
1.3 to 1.6 ns per node per frame on the planning machine
(`benchmarks/rp21-graph-cost.json`). Where that matters, compile the graph.

## Compile graphs into a kernel library

The compiled tier writes C++ that composes RackPy's node types (from
`rackpy/graph.hpp`) statically, one kernel type per graph, registered in a
kernel library. Write the graphs in a Python file as a dict of kernel type
names to graphs (`GRAPHS`, unless you pass `--variable`), then:

```bash
bin/rackpy-graph emit tests/dsp/in_rack_libraries/graphs.py graphs --output graphs.cpp
```

Build `graphs.cpp` as any [kernel library](dsp.md#write-a-kernel-in-c), list
it in `rackpy-loader.json`, and use its types like any kernel:
`rackpy.dsp.kernel.load("graphs.Tutorial")()`. A type's fields are the
graph's wiring (`input[0]`, `light[0]`, ...), each defaulting to the graph's
own index, then its controls; `-1` is refused. RackPy's tests compile
exactly this example (`tests/dsp/kernel_library/graphs.cpp`, checked to be
current) and run every kernel against the plan interpreter inside Rack.

- **Exact.** Each node is the same function the interpreter calls, and a
  register barrier after each node (`settle`) stops the SDK's
  `-funsafe-math-optimizations` from folding or reassociating across nodes,
  so the two tiers agree bit for bit.
- **Fast when it can be.** While every input and delay carries at most one
  channel (so every signal does), a compiled kernel computes one value per
  node; otherwise it runs the general 0 to 16 channel code.
- **RackPy's node types only.** Graphs that use native extension nodes are
  refused (their types live in other libraries); compile those with
  `Graph.compile()`.
- **Trusted.** `emit` runs the Python file you give it.

On the planning machine a compiled mono plan costs about 1.5 to 1.7 ns per
frame more than the same DSP fused by hand in C++, RackPy's own fixed cost
per kernel module included, and 16 voices run faster than a hand-written
loop (`benchmarks/rp21-graph-cost.json`).

## Build and check a graph

```{rackpy-example} graph_offline.py
:caption: A VCA, a blinking light and a counter, checked with the offline reference
```

Arithmetic and comparisons on signals build nodes: `a * b + 1` is three
nodes (a constant among them), and `x < 0.5` is a signal of 1 and 0. Numbers
become constants. {py:meth}`~rackpy.dsp.graph.Graph.plan` checks the graph
and lowers it to a {py:class}`~rackpy.dsp.graph.Plan`: the nodes the outputs
and lights need, in execution order, each reading only earlier nodes.

## Arbitrary Python is not compiled

A graph is made only of registered native node types. Python functions,
lambdas and branches on signals are not translated into native code, and
RackPy refuses them rather than guessing: `if signal:`, `float(signal)` and
`math.sin(signal)` raise `TypeError`, and passing anything other than a
signal or a number to a node raises
{py:class}`~rackpy.dsp.graph.GraphError`. Choose per sample with
{py:meth}`~rackpy.dsp.graph.Graph.where`, and use the node functions
({py:meth}`~rackpy.dsp.graph.Graph.sin2pi`,
{py:meth}`~rackpy.dsp.graph.Graph.pitch_to_freq`, ...) instead of `math`.

## Nodes

| Node | What it gives |
| --- | --- |
| `input(port)` | an input port's voltages; its channels are the cable's (0 when unpatched) |
| `param(index)`, `constant(value)` | a parameter's value; a fixed number (mono) |
| `control(name, default)` | a number Python may change while the graph runs, by name (mono) |
| `sample_rate()`, `sample_time()` | the engine's sample rate, and one frame's duration (mono) |
| `+ - * /`, `-x`, `abs(x)` | IEEE float arithmetic per channel (division by zero gives an infinity or NaN) |
| `< <= > >=` | 1 where true, else 0 |
| `minimum`, `maximum`, `clamp`, `where` | `std::fmin`, `std::fmax`, `rack::math::clamp`, and a choice per channel |
| `pitch_to_freq(v)` | `dsp::FREQ_C4 * std::pow(2.f, v)`: V/oct to Hz, 0 V being C4 |
| `sin2pi(x)` | `std::sin(2.f * float(M_PI) * x)` |
| `phasor(freq)` | a phase from 0 to 1 per channel, kept as Rack's tutorial oscillator keeps it |
| `schmitt(x, low, high)` | Rack's `dsp::SchmittTrigger`: 1 on the frame `x` rises to `high` |
| `pulse(trigger, duration)` | Rack's `dsp::PulseGenerator`: 1 for `duration` seconds after each trigger |
| `delay(initial)` | the signal it is fed, one frame later |
| `output(port, signal)`, `light(index, signal)` | write an output (at least one channel) or a light's brightness (channel 0) |
| `expander_send(side, signal, lane)`, `expander_receive(side, lane)` | send a signal to the module beside it, and receive one, a frame later: see [expanders](#expanders) |
| `audio_input(channel)`, `audio_output(channel, signal)` | device input and output samples: see [audio and MIDI](#audio-and-midi) |
| `midi_cv(output, ...)` | one of Core MIDI-CV's outputs from a timed MIDI input: see [audio and MIDI](#audio-and-midi) |

Every operation computes in float32, as Rack does.

## Audio and MIDI

The RP-31J I/O path is **work in progress**. Inside Rack, a module can attach
an audio port and MIDI queues before the engine owns it; the native graph
interpreter and generated kernel libraries can access their per-frame data.
See [MIDI](../reference/midi.md) for attachment and ownership constraints.
The offline reference refuses these nodes because it has no device endpoint.

`audio_input(channel)` reads device units (full scale 1), returning zero when
no channel is available. `audio_output(channel, signal)` sends the sum of the
signal's channels in those same units; write each device channel at most once.
Multiply a device sample by 10 to produce Rack's usual voltage scale, and
scale Rack voltages down before sending them to a device. A graph with only
an audio output is a valid sink. The bridge buffers device blocks; startup
and sample-rate transition equivalence to Core Audio is still under study.

`midi_cv(output, input=0, ...)` processes messages due on one of the module's
four MIDI inputs. The output index 0-11 follows `rackpy.MidiToCV`'s order
(pitch, gate, velocity, aftertouch, pitch wheel, modulation wheel, retrigger,
clock, divided clock, start, stop, continue). Each node owns an independent
parser and settings, including polyphony, priority modes and wheel smoothing;
use the same settings for nodes meant to represent one MIDI-CV module. A
compiled graph uses RackPy's I/O services and runs the polyphonic path.

## Channels

Every signal has 0 to 16 channels per frame, as Rack's cables do, and the
count can change from one frame to the next. An operation has as many
channels as its widest operand: a mono operand applies to every channel, and
channels missing from a shorter one read 0 V. Oscillators, triggers and
pulses have at least one channel, and an output always gets at least one,
as `Port::setChannels` does for a patched output.

## Feedback

Rack processes one sample at a time, so feedback within a module needs one
sample of delay. Make it explicit with
{py:meth}`~rackpy.dsp.graph.Graph.delay` and give it its source with
`feed()`. {py:meth}`~rackpy.dsp.graph.Graph.forward` names a signal before it
is defined, without adding any delay, so a cycle built with forward
references alone is refused, naming each node in the cycle and the line
that made it. RackPy never adds latency to make a graph run.

## Native extension nodes

RackPy's node types cover oscillators, arithmetic, triggers, lights and
polyphony. For anything else, a [kernel library](dsp.md#write-a-kernel-in-c)
adds native node types in C++: it implements `rackpy::graph::Node`
(`process` reads its input signals and writes its output signal, channel
count included, under the same real-time rules as a kernel) and registers
`rackpy::graph::NodeType`s with `RACKPY_GRAPH_NODES`, under its namespace.
Python uses them by name:

```{rackpy-conceptual}
:caption: Inside Rack, with the example kernel library loaded (its test runs this)
folded = g.node("example.Fold", g.input(0), drive=2.0, limit=5.0)
held = g.node("example.Hold", folded, g.input(1))
```

{py:func}`~rackpy.dsp.graph.node_types` lists the types loaded, with their
inputs and argument names. The node type, its inputs and its arguments (by
name, in any order, all required) are checked when the graph is compiled;
the type's own `create` may refuse its arguments (`ValueError`). A node that
writes a channel count outside 0 to 16 fails the plan: the module is
silenced and the failure is recorded, as for a failing kernel. The plan
interpreter calls an extension node through one virtual call per frame, and
the offline reference cannot run one.

## Expanders

A graph can exchange signals with the modules beside it, through Rack's
[expanders](../reference/engine-module.md#expanders) (RP-23B).
{py:meth}`~rackpy.dsp.graph.Graph.expander_send` writes a signal into a lane
of the neighbour's message on one side (0 left, 1 right), and
{py:meth}`~rackpy.dsp.graph.Graph.expander_receive` reads what the neighbour
on a side sent. A message arrives **exactly one frame** after it was sent,
in whatever order and on however many threads Rack's engine processes the
two modules, because Rack swaps each side's two buffers at the end of the
frame:

```{rackpy-example} graph_expanders.py
:caption: A module and its expander, stepped on a laboratory bench and by the offline reference
```

- **Messages.** A module receives in its own messages: give the side it
  receives on `"rackpy.graph"` messages with
  {py:func}`rackpy.engine.expanders.graph_messages` (one lane per signal, up
  to 64), before the module goes to Rack's engine. The sender writes into
  the neighbour's producer message and requests the flip; the receiver reads
  its consumer message. Each lane holds a channel count and 16 voltages.
- **Checks.** On Rack's engine thread, before touching anything, a send
  checks that the neighbour is a RackPy module, that its facing expander
  points back to the sender, and that its messages there have the
  `"rackpy.graph"` schema and room for the lane; a receive checks that the
  neighbour is a RackPy module and that its own messages fit. Otherwise
  nothing is sent, and the receive gives 0 channels. These checks take no
  lock and allocate nothing; a foreign plugin's pointer is never read.
- **Read once.** A receive marks its lane read. So when the neighbour is
  removed, moved away, or stops sending, the next frame receives 0 channels
  instead of repeating the last message.
- **Lanes.** Each lane of each side is sent by one node and received by one
  node at most; a graph that only sends is valid.

Both tiers compose these nodes: the compiled tier
({py:mod}`rackpy.dsp.graph_codegen`) calls RackPy's expander services, which
RackPy gives the generated library when it loads it, and takes the
polyphonic path for a graph that exchanges, since a received lane's channel
count is known only when it is read. Graphs exchange with RackPy modules
only; a C++ kernel library can define [message layouts of its
own](dsp.md#exchange-expander-messages-from-c). To watch what arrives while
Rack's engine runs, turn on `expander.snapshots` and read
`expander.snapshot()` ([snapshots](../reference/engine-module.md#expanders)).
{py:func}`rackpy.dsp.graph_reference.run_pair` evaluates two graphs side by
side, one frame apart, and the live-Rack tests check Rack's own engine
against it frame for frame, at 44.1, 48 and 96 kHz with 1, 2 and 4 engine
threads, for both tiers.

## The offline reference

{py:func}`rackpy.dsp.graph_reference.run` evaluates a graph for a number of
frames, given each patched input's channels per frame, parameter values,
control changes by frame, sample-rate changes and what each expander lane
receives; its result records what each lane sent. It rounds every operation
to float32. Arithmetic, comparisons, `minimum`, `maximum` and `clamp` match
float32 exactly; `sin2pi` and `pitch_to_freq` round Python's double-precision
functions, which may differ from the C library's by one unit in the last
place. It is thousands of times slower than native code and is never the
production path.
