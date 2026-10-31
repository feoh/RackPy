# Oscillator

An original RackPy plugin with the feature of the module that Rack's
[Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial)
builds: a sine oscillator whose pitch is a knob plus a 1 V/octave input, with
0 V at middle C (261.63 Hz), a +-5 V output, and a light that blinks at 1 Hz.
No manual code is copied ([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project (bin/rackpy-plugin create)
res/Oscillator.svg                               the original panel, with its components layer
pythonpath/rackpyoscillator/__init__.py          the entry package: adds the model
pythonpath/rackpyoscillator/oscillator.py        the module and its widget (add-module, then edited)
pythonpath/rackpyoscillator_dsp.py               the DSP as a graph, and the offline reference
```

## How it works

`rackpyoscillator_dsp.oscillator_graph()` describes the processing with
`rackpy.dsp.graph`: pitch to frequency, a phasor, a sine at 5 V, and a 1 Hz
phasor compared with 0.5 for the light. `configure()` compiles it
(`Graph.compile()`) and gives each module its own native kernel, which Rack's
engine runs every sample. **Python never runs per sample**; it only configures
the module, on Rack's main thread.

`rackpyoscillator_dsp.offline_reference()` evaluates the same graph in pure
Python, for teaching and tests. Rack never calls it. That module does not
import Rack, so it runs anywhere:

```bash
PYTHONPATH=examples/oscillator/pythonpath uv run python -c '
from rackpyoscillator_dsp import offline_reference
print(offline_reference(4, sample_rate=48000).outputs[0])'
```

prints the first four frames, `[[0.171...], [0.342...], [0.512...], [0.682...]]`.

The knob is configured from -3 V to 3 V and displays as hertz
(261.63 Hz x 2^volts). A polyphonic cable into the input gives a polyphonic
output; an unpatched input gives one channel.

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/oscillator --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` in the user folder says `Loaded plugin RackPyOscillator
2.0.0`, and the browser lists *RackPy Oscillator*. Patch its output to an
audio interface to hear middle C at the knob's default; the light blinks once
a second. Remove `plugins-lin-x64/RackPyOscillator` before building again.

## What the tests check

`tests/examples/test_oscillator.py`, over fixed one-second input streams (the
knob stepped through 0, 1 and -1 V unpatched; the input at 0.5 then -0.25 V
patched) at 44.1, 48 and 96 kHz:

- the offline reference and a C++ reference
  (`native/tests/oscillator_reference.cpp`, the oscillator as a C++ plugin
  author writes it on Rack's own `Param`, `Input`, `Output` and `Light`) both
  give 1 V/octave from middle C within 0.02%, peaks of 5 V, no step steeper
  than the sine allows, and a 1 Hz, 50% blink;
- in Rack (test-hooks build), the module made from this model matches the C++
  reference within 0.05 V at every frame, with the same light, and keeps
  those properties; Initialize restarts it; a sample-rate change keeps the
  waveform continuous; the browser preview builds without a module; it runs in
  Rack's engine and over a cable;
- in windowed Rack, a patch of two oscillators joined by a cable carries the
  sine, a module added through the browser's path can be deleted and the
  deletion undone, and the autosave reopens with the knob and cable;
- built on a production bundle, the plugin loads under its own slug,
  `RackPyOscillator`, and keeps a two-module, one-cable patch over two launches.

## Numerical differences from the C++ module

- **Phase convention.** The graph's phasor keeps its phase in [0, 1); the
  usual C++ form keeps it in [-0.5, 0.5). The sine is the same, but float
  rounding differs, so the two drift apart slowly: at most 0.014 V after one
  second in these streams, measured in Rack. The frequency is the same.
- **The blink's timing** comes from summing a float32 sample time, which runs
  up to 0.1% fast or slow per second (31 frames at 48 kHz); the C++ module's
  does the same, and the tests allow 0.2%.
- **Initialize.** RackPy resets a module's kernels on Initialize, so the
  oscillator restarts at phase 0 and the light comes on. A C++ module that does
  not override `onReset` keeps its phase.

## Restrictions

Linux x86_64 and Rack Free 2.6.6 only, so far. Only one RackPy plugin can run
in a Rack process. The plugin's Python is trusted code, not a sandbox. Do not
distribute development builds. See [the template](../template/README.md#restrictions).
