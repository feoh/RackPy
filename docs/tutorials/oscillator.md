# Tutorial: a sine oscillator

In this tutorial you make the module that Rack's own
[plugin tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial) makes,
in Python: a sine oscillator with a pitch knob, a pitch input, an output and a
blinking light. You will learn how Rack thinks about signals, and how a RackPy
module processes audio without running Python sample by sample.

| | |
| --- | --- |
| Time | About 30 minutes, after the [quickstart](../quickstart.md) |
| You need | The quickstart's checkout, RackPy built and bundled, and its `MyPlugin` project |
| Versions | RackPy {{rackpy_version}} (unreleased), Rack Free {{rack_version}}, Linux x86_64 |
| Finished code | `examples/oscillator`, tested in Rack |

The finished plugin is [examples/oscillator](../../examples/oscillator/README.md).

## Signals are voltages

Rack imitates a hardware modular synthesizer, where modules exchange electrical
signals through patch cables. In Rack every signal is a number of **volts**,
and the conventions are those of hardware:

- **Audio** swings between about −5 V and +5 V. A sine at ±5 V is a full-scale
  tone.
- **Pitch** uses **1 V per octave**: one volt more is twice the frequency.
  0 V is middle C, 261.63 Hz, so 1 V is C5 (523.25 Hz) and −1 V is C3
  (130.81 Hz).
- **Gates and triggers** are 0 V for off and 10 V for on.

Rack computes every module once per **sample**: 48,000 times a second at
48 kHz. Each time, a module reads its inputs and knobs and writes its outputs
and lights. The oscillator's output is just the right voltage for each sample.

## Python configures, native code processes

A sample lasts about 21 microseconds at 48 kHz, too short for Python to run in
reliably. So a RackPy module splits the work:

1. **Python, once:** when Rack creates a module, your Python names its
   controls and describes its processing as a **graph**: inputs, arithmetic,
   oscillators and outputs, connected together.
2. **Native code, every sample:** RackPy compiles the graph into a native
   **kernel**, and Rack's engine runs that kernel for each sample. Python is
   not involved.

The graph is ordinary Python, and RackPy can also run it slowly in pure Python,
which is how you will check it before starting Rack.

## 1. Describe the processing

Copy the example plugin's DSP file into your plugin, next to its package:

```bash
cp examples/oscillator/pythonpath/rackpyoscillator_dsp.py MyPlugin/pythonpath/myplugin_dsp.py
```

It holds the oscillator's graph, and `offline_reference`, which runs the graph
in pure Python for checking. The graph:

```{rackpy-example-source} oscillator/pythonpath/rackpyoscillator_dsp.py
:pyobject: oscillator_graph
```

Reading it from the inside out, for each sample:

- `g.param(...) + g.input(...)` is the pitch in volts: the knob plus whatever
  is patched into the input (0 V when nothing is).
- `g.pitch_to_freq(pitch)` turns volts into hertz: 261.63 Hz × 2^volts.
- `g.phasor(freq)` is a ramp from 0 to 1 that repeats `freq` times a second:
  the oscillator's phase.
- `g.sin2pi(...)` is the sine of 2π times the phase, between −1 and 1, and
  `AMPLITUDE *` scales it to ±5 V for the output.
- The light is lit while a 1 Hz phase is below 0.5: half of every second.

If the input carries a polyphonic cable, with several voltages on one cable,
the graph runs once per channel and the output carries as many channels.

## 2. Check it before starting Rack

`offline_reference` from the same file runs the graph for a number of frames
and returns what the module would output. One second at 48 kHz, with the knob
at 0 V:

```{rackpy-example} oscillator_offline.py
:caption: Expected signal, computed by the graph itself
```

This is what the oscillator should do: it starts at 0 V and rises (the first
samples climb by about 0.17 V each), it peaks at ±5 V, it completes 261 cycles
in a second (middle C is 261.63 Hz), the light is on for half the time, and one
volt more doubles the frequency. The light's half second is not exactly 24,000
frames because RackPy accumulates time in 32-bit floating point, as Rack's
own modules do.

## 3. Configure the module

Open `MyPlugin/pythonpath/myplugin/mymodule.py`, which `add-module`
generated. It numbers the controls it found on the panel (`Param.PITCH`,
`Input.PITCH`, `Output.SINE`, `Light.BLINK`), has a `configure` function,
which Rack calls for each new module, and a widget class that draws the panel.
Make three changes.

First, add two imports at the top: `from myplugin_dsp import oscillator_graph`
and `from rackpy.raw.dsp import FREQ_C4` (middle C's frequency).

Second, after the `Light` class, build the graph once, for all modules:
`GRAPH = oscillator_graph(Param.PITCH, Input.PITCH, Output.SINE, Light.BLINK)`.

Third, replace `configure` with the example's:

```{rackpy-example-source} oscillator/pythonpath/rackpyoscillator/oscillator.py
:pyobject: configure
```

- `config(...)` says how many knobs, inputs, outputs and lights the module
  has, from the `IntEnum` classes.
- `config_param(...)` gives the knob its range in volts (−3 to +3 octaves
  around middle C) and its default (0 V). The last two arguments make Rack
  show it in hertz: 261.63 × 2^volts.
- `config_input`, `config_output` and `config_light` name the ports and the
  light: Rack shows the names when you hover over them.
- `set_kernel(GRAPH.compile())` compiles the graph and hands the kernel to the
  module. Each module gets its own kernel, so each oscillator keeps its own
  phase.

The widget class needs no change: it loads the panel and places Rack's knob,
port and light widgets where the panel's components layer put them.

## 4. Hear it

Quit Rack, delete `~/.local/share/Rack2/plugins-lin-x64/MyPlugin`, and build
again, as in the quickstart:

```bash
bin/rackpy-plugin build MyPlugin --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack and add *My Module*. Its light blinks once a second. Patch its
output to the *Audio* module's input, choose your sound card there, and you
hear middle C. Turn the knob: its tooltip shows the frequency in hertz. Patch
another oscillator's output into the pitch input, and the second one's ±5 V
sine sweeps the first one's pitch five octaves up and down.

The finished example is tested exactly like this in Rack:
[tests/examples/test_oscillator.py](../../tests/examples/test_oscillator.py)
checks, at 44.1, 48 and 96 kHz, that it follows 1 V per octave from middle C
within 0.02%, peaks at 5 V and blinks at 1 Hz, and that it matches the same
oscillator written in C++ against Rack's own classes to within 0.05 V. The
example's README lists the [numerical differences](../../examples/oscillator/README.md#numerical-differences-from-the-c-module).

## What you learned

- Rack's signals are voltages: ±5 V audio, 1 V per octave pitch from middle C.
- Rack processes every module once per sample.
- A RackPy module's Python names its controls and describes its processing as
  a graph, once. Rack runs the compiled kernel every sample.
- You can run a graph in pure Python to check what it does.

Next: [processing audio](../how-to/dsp.md) and [graphs](../how-to/graphs.md)
cover every node type, polyphony and kernels written in C++, and
[the configuration example](../../examples/configuration/README.md) adds more
controls, menus and presets. If the module does not appear, see
[troubleshooting](../troubleshooting.md).
