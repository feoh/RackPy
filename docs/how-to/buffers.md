# Read and write a port's voltages in bulk

Each Rack port holds an array of 16 voltages, one per channel. Python can read
and write them one at a time, as a checked view, as a zero-copy buffer, or four
at a time as a SIMD vector. This guide shows when each is allowed, and why.

**Status:** Linux x86_64, Rack Free {{rack_version}}; the example runs inside
Rack in the docs tests.

:::{admonition} Not for audio
:class: note

All of this runs on Rack's main thread, between frames, for setting up,
inspecting and testing. A module's audio is processed by its native kernel
([process audio](dsp.md)); Python never processes samples in Rack.
:::

```{rackpy-example} buffers.py
:caption: The ways to reach a port's voltages, on a port Python owns
```

## Which to use

| Form | Gives | Allowed on |
| --- | --- | --- |
| `port.get_voltage(c)`, `port.set_voltage(v, c)` | one channel | any port you can reach |
| `port.voltages` | a checked view: indexing reads and writes the port, negative indices count from the end | any port you can reach |
| `memoryview(port.voltages)` | a zero-copy `float` buffer of the port's array, for `array`, NumPy or `float_4.load` | **only a port Python owns**; it keeps the port alive |
| `port.voltages.snapshot()` | an independent `array('f')` copy | any port |
| `port.get_voltage_simd(first)`, `set_voltage_simd(v, first)` | four channels as a `float_4` | any port; `first` is 0 to 12 |

A module that Rack's engine owns (every module in a patch) has Rack-owned
ports, and `memoryview()` on them raises `BufferError`. A raw buffer could
outlive the module, and then read freed memory; RackPy refuses rather than copy
behind your back. Take a `snapshot()` instead.

Channel numbers are checked where C++ would read past the array: an index
outside the 16 voltages raises `IndexError`, and a channel count above 16
`ValueError`. The [engine components reference](../reference/engine-components.md#the-voltage-array-views-and-copies)
and [SIMD](../reference/simd.md#lanes-views-and-buffers) have the details.
