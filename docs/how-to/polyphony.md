# Make a module polyphonic

A Rack cable carries 1 to 16 **channels**, one voltage each: a polyphonic
cable from a MIDI-CV module carries one pitch per held note. A polyphonic
module processes every channel of its inputs and gives its outputs as many.
This guide shows the two ways a RackPy module does it.

**Status:** Linux x86_64, Rack Free {{rack_version}}. Both ways are tested in
Rack against C++ modules ([examples/polyphony](../../examples/polyphony/README.md),
[examples/oscillator](../../examples/oscillator/README.md)).

## With a graph: nothing to do

A [graph](graphs.md) is polyphonic already. Every signal in it has 0 to 16
channels, and an operation has as many as its widest operand
([channels](graphs.md#channels)). The [oscillator tutorial](../tutorials/oscillator.md)'s
module plays one voice per channel of its pitch input, with no extra code:

```{rackpy-example-source} oscillator/pythonpath/rackpyoscillator_dsp.py
:pyobject: oscillator_graph
```

The knob (`g.param`) is mono, so it applies to every channel; the input's
channels set how many voices there are, and the output gets that many. With
nothing patched, the input has 0 channels and the output 1.

## With a kernel in C++: loop over the channels

A kernel written in C++ ([process audio](dsp.md#write-a-kernel-in-c)) does
what a C++ module's `process()` does: it reads the input's channel count, sets
the output's, and loops. The polyphony example has two such kernels in
[kernels/polyphony.cpp](../../examples/polyphony/kernels/polyphony.cpp):

- `polyphony.Scalar` keeps one phase per channel and processes the channels
  one after another.
- `polyphony.Simd` processes four channels at a time with Rack's `float_4`
  ([SIMD](../reference/simd.md)): five channels cost two steps, and it outputs
  five.

The Python side only loads the kernel type and hands it the module's port and
parameter numbers:

```{rackpy-example-source} polyphony/pythonpath/rackpypolyphony/polysine.py
:pyobject: configure_with
```

`bin/rackpy-plugin build --kernels` copies the compiled library into the
plugin; the example's README has the commands.

To read the two loops in Python, `rackpypolyphony_teaching.py` writes them in
plain Python. That is for reading only: Python never processes audio in Rack.

## Port rules that apply either way

- An input's `get_channels()` is 0 when nothing is patched, 1 for a mono
  cable and up to 16 for a polyphonic one.
- An output that is patched must have at least one channel. Set it with
  `set_channels()`; a graph and RackPy's kernels do this for you.
- `get_voltage(c)` reads channel `c`. Channels above the count are not part
  of the signal; Rack sets them to 0 V when the count shrinks.

The [engine components reference](../reference/engine-components.md) has the
full `Port` API, and [values](../reference/values.md) explains the voltage
buffers Python can read.
