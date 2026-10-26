# Describe DSP as a graph

A **graph** describes a module's per-sample processing in Python, built from
native node types: ports, parameters and controls in, arithmetic,
oscillators and Rack's own triggers in between, outputs and lights out.
Python only describes it; nothing in a graph runs Python per sample.

**What is available today (RP-21A):** {py:mod}`rackpy.dsp.graph` builds,
validates and lowers graphs, and {py:mod}`rackpy.dsp.graph_reference`
evaluates them offline, in Python, for tests and teaching. Running a graph
inside a module, on Rack's engine thread, needs the native plan interpreter
(RP-21B) or the compiled tier (RP-21C), which are **not available yet**.
Until then, use a [kernel](dsp.md) for a module's audio path.

Both modules are plain Python and work outside Rack.

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

Every operation computes in float32, as Rack does.

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

## The offline reference

{py:func}`rackpy.dsp.graph_reference.run` evaluates a graph for a number of
frames, given each patched input's channels per frame, parameter values,
control changes by frame and sample-rate changes. It rounds every operation
to float32. Arithmetic, comparisons, `minimum`, `maximum` and `clamp` match
float32 exactly; `sin2pi` and `pitch_to_freq` round Python's double-precision
functions, which may differ from the C library's by one unit in the last
place. It is thousands of times slower than native code and is never the
production path.
