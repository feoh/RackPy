# Pass signals to the module beside yours

In Rack, two modules placed edge to edge are each other's **expanders**: each
module sees its left and right neighbours. A module can send its neighbour
messages, or read the neighbour's knobs and ports directly. This guide does
both.

**Status:** Linux x86_64, Rack Free {{rack_version}}, tested in Rack
([examples/expanders](../../examples/expanders/README.md)).

## Send messages with graphs

Rack's expander messages use two buffers per side, owned by the **receiving**
module: the sender writes into one while the receiver reads the other, and
Rack swaps them after every module of a frame has run. A message arrives
exactly one frame after it was sent, however Rack orders the two modules.

1. **The receiver gives its side buffers**, in its constructor callback, with
   {py:func}`rackpy.engine.expanders.graph_messages`: here, on its left, with
   room for two signals (lanes).

   ```{rackpy-example-source} expanders/pythonpath/rackpyexpanders/receiver.py
   :pyobject: configure
   ```

2. **The sender's graph sends** each lane to its right neighbour with
   `expander_send`, and **the receiver's graph receives** it from its left
   with `expander_receive`:

   ```{rackpy-example-source} expanders/pythonpath/rackpyexpanders_graphs.py
   :pyobject: source_graph
   ```

   ```{rackpy-example-source} expanders/pythonpath/rackpyexpanders_graphs.py
   :pyobject: receiver_graph
   ```

RackPy checks the neighbour on the engine thread before writing: if it is not
a RackPy module, or its buffers are of another kind or too small, nothing is
written, and the receiver's signal has 0 channels. Moving the modules apart
does the same at once. [Graphs](graphs.md#expanders) has the details, and how
to exchange messages from a C++ kernel is in
[process audio](dsp.md#exchange-expander-messages-from-c).

## Read the neighbour directly, in C++

A C++ module can read its neighbour's parameters and ports through Rack's
`leftExpander.module`. That happens on the engine thread, where Python never
runs, so in RackPy it is a C++ kernel's job: the example's Peeker
([kernels/expanders.cpp](../../examples/expanders/kernels/expanders.cpp))
checks that its left neighbour is this plugin's Source, then reads its knob
and output.

The value it reads is from this frame or the last, because Rack may process
the two modules in either order, or at once on two threads. When the timing
matters, use messages.

## From Python, on the main thread

Python can look at a module's expanders too ({py:mod}`rackpy.engine.expanders`):
which module is beside it, and the contents of a message buffer while Rack's
engine is not running the module. RackPy refuses to touch a buffer that the
engine thread is writing, rather than read it while it changes. Python is for
setting up and inspecting; the per-sample exchange is native.
