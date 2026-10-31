# Polyphony

An original RackPy plugin with the features of the Plugin API Guide's
[polyphony](https://vcvrack.com/manual/PluginGuide#Polyphony) and
[SIMD library](https://vcvrack.com/manual/PluginGuide#SIMD-library) sections:
a polyphonic oscillator, written one channel at a time and four channels at a
time with Rack's `float_4`. No manual code is copied
([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project; rackpy-plugin.json lists the kernel library
res/PolySine.svg                                 the original panel, with its components layer
kernels/polyphony.cpp                            the kernel library: polyphony.Scalar and polyphony.Simd
pythonpath/rackpypolyphony/__init__.py           the entry package: adds both models
pythonpath/rackpypolyphony/polysine.py           the modules and their widget
pythonpath/rackpypolyphony_teaching.py           both loops in plain Python, offline (teaching only)
```

## The two oscillators

**Poly sine (SIMD)** (`PolySine`) and **Poly sine (scalar)**
(`PolySineScalar`) are the same module: a PITCH knob in volts from middle C,
a polyphonic V/OCT input and a polyphonic sine output at ±5 V. A cable with N
channels gives N voices and an N-channel output; an unpatched input gives one
voice at the knob's pitch. They differ in the native kernel Rack's engine runs
every frame, from `kernels/polyphony.cpp`:

- `polyphony.Scalar` keeps a `Voice` (its phase) per channel and processes
  each channel in turn.
- `polyphony.Simd` keeps four phases per `float_4` and processes four channels
  per step with `simd::pow` and `simd::sin`. For 5 channels it computes 8 and
  outputs 5.

Both are C++ because the per-channel loop is the audio path: Python only
configures the module. (A [graph](../../docs/how-to/graphs.md) can also be
polyphonic without C++, as the [oscillator](../oscillator/README.md) is; this
example shows how a C++ kernel does it, and Rack's SIMD types.)

`rackpypolyphony_teaching.py` writes both loops in plain Python, to read
alongside the C++: `per_channel` and `four_at_a_time`. **They are for
teaching and tests only: not real-time, and Rack never runs them.**

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. RackPy's
build compiles every example's kernel library with the Rack SDK's plugin
flags. From the repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/polyphony --bundle /tmp/rackpy-bundle/RackPy \
  --kernels build/dev/examples/polyphony/kernels --dest ~/.local/share/Rack2/plugins-lin-x64
```

`--kernels` copies `polyphony.so` into the plugin's `kernels/` folder and lists
it in `rackpy-loader.json`, so RackPy loads it before the entry package. Start
Rack: `log.txt` says `RackPy: kernel type polyphony.Simd from
kernels/polyphony.so` and `Loaded plugin RackPyPolyphony 2.0.0`. Patch a
polyphonic V/OCT source (a MIDI-CV module with several channels) into V/OCT.
Only one RackPy plugin runs in a Rack process, so remove other RackPy plugins
first, and remove `plugins-lin-x64/RackPyPolyphony` before building again.

## What the tests check

`tests/examples/test_dsp_expanders.py`:

- offline: the two teaching loops agree for 0, 1, 5 and 16 channels;
- natively: `native/tests/polyphony_reference.cpp`, the oscillator as a C++
  module's `process()` on Rack's own ports, scalar and `float_4`, over five
  scenarios (unpatched, mono, five voices, sixteen, and sixteen then three):
  the two versions agree within 2e-5 V (measured: at most 1.3e-5 V), and the
  Python teaching loop within 1e-2 V (measured: at most 4.3e-4 V, because
  Python's floats are doubles);
- the kernel library references no CPython symbol, and each kernel makes no
  heap, lock, write or GIL call over 20 000 frames of changing polyphonic
  input under RackPy's audio guard;
- in Rack (test-hooks build), both modules, stepped through the reference's
  scenarios, match the C++ within 1e-5 V (measured: 5e-9 V), channel counts
  included;
- built on a production bundle with its kernel library, the plugin loads
  under its own slug and keeps its modules over two launches.

## Restrictions

Linux x86_64 and Rack Free 2.6.6 only, so far. A kernel library is native
code built for one platform; macOS and Windows builds are not made or tested.
Only one RackPy plugin can run in a Rack process. The plugin's Python and C++
are trusted code, not a sandbox. Do not distribute development builds. See
[the template](../template/README.md#restrictions).
