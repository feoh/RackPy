# Expanders

An original RackPy plugin with the features of the Plugin API Guide's
[expanders](https://vcvrack.com/manual/PluginGuide#Expanders) section: a module
reading the module beside it, and two modules exchanging messages. No manual
code is copied ([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project; rackpy-plugin.json lists the kernel library
res/Source.svg, res/Receiver.svg, res/Peeker.svg the original panels, with their components layers
kernels/expanders.cpp                            the kernel library: expanders.Peek
pythonpath/rackpyexpanders/__init__.py           the entry package: adds the three models
pythonpath/rackpyexpanders/source.py             the producer
pythonpath/rackpyexpanders/receiver.py           the consumer
pythonpath/rackpyexpanders/peeker.py             the module that reads its neighbour
pythonpath/rackpyexpanders_graphs.py             the Source's and the Receiver's graphs (no Rack)
```

Rack makes two modules side by side each other's *expanders*: each module
sees the module on its left and on its right. There are two ways to use that.

## Messages: the Source and the Receiver

Put a **Receiver** directly to the right of a **Source**. The Source scales
its input by LEVEL, outputs it, and sends it, and LEVEL itself, to its right
neighbour; the Receiver outputs what arrives, and its light shows the
Source's LEVEL. Both are [graphs](../../docs/how-to/graphs.md#expanders)
compiled into native kernels:

- The **receiving** module owns the buffers, as in Rack: the Receiver's
  constructor callback gives its left side two buffers of RackPy's
  `"rackpy.graph"` message schema with room for two lanes
  (`graph_messages(module.left_expander, 2)`).
- The Source's graph writes its two lanes into the neighbour's producer buffer
  and requests the flip (`expander_send`); the Receiver's reads its consumer
  buffer (`expander_receive`).
- Rack swaps the buffers after every module of a frame has run, so a message
  arrives **exactly one frame** after it was sent, in whatever order and on
  however many engine threads Rack runs the two.
- RackPy checks the neighbour on the engine thread first: a module that is not
  a RackPy module, or whose buffers have another schema or too few lanes, gets
  nothing written, and receives nothing. With no Source on its left, the
  Receiver outputs 0 V and its light is off.

## Reading the neighbour: the Peeker

Put a **Peeker** directly to the right of a Source. Its C++ kernel
(`kernels/expanders.cpp`) does what a C++ module does with Rack's
`leftExpander.module`: it checks that the neighbour is this plugin's Source
(same plugin, model slug `Source`, enough params and ports), then reads its
LEVEL knob, output as 0 to 10 V, and its output, copied. The light shows that
a Source is there. Beside anything else it reads nothing. This needs C++: a
neighbour's params and ports are read on the engine thread, where Python never
runs.

What the Peeker reads is the Source's value from this frame or the last,
because Rack may process the two in either order, or at once on two engine
threads. When timing matters, use messages.

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/expanders --bundle /tmp/rackpy-bundle/RackPy \
  --kernels build/dev/examples/expanders/kernels --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` says `Loaded plugin RackPyExpanders 2.0.0`. Place a Source,
then a Receiver or a Peeker touching its right edge. Only one RackPy plugin runs
in a Rack process, so remove other RackPy plugins first, and remove
`plugins-lin-x64/RackPyExpanders` before building again.

## What the tests check

`tests/examples/test_dsp_expanders.py`:

- offline (`graph_reference.run_pair`): the Receiver outputs what the Source
  sent one frame earlier, and alone it outputs 0 V with its light off;
- in Rack's own engine, stepped by the test on 1, 2 and 4 engine threads,
  with a counter cabled into the Source: for 40 frames the Receiver's output
  equals the Source's of the frame before; its light shows the Source's LEVEL;
  when another Source moves in on its left it follows that one; when that
  Source is removed it outputs 0 V and its light goes off at once;
- in the same engine: the Peeker copies the Source's output (this frame's or
  the last's), outputs LEVEL as volts and follows a changed knob; beside a
  Receiver it reads nothing; after its Source is removed, nothing, with no
  expander left;
- the kernel library references no CPython symbol and `expanders.Peek`, with
  a Source beside it, makes no heap, lock, write or GIL call over 20 000
  frames under RackPy's audio guard;
- built on a production bundle with its kernel library, the plugin loads
  under its own slug and keeps its three modules over two launches.

## Restrictions

Linux x86_64 and Rack Free 2.6.6 only, so far. Modules exchange messages only
with RackPy modules; a C++ kernel library can define [its own message
schema](../../docs/how-to/dsp.md#exchange-expander-messages-from-c). Only one
RackPy plugin can run in a Rack process. The plugin's Python and C++ are
trusted code, not a sandbox. Do not distribute development builds. See
[the template](../template/README.md#restrictions).
