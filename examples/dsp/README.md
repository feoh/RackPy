# DSP

An original RackPy plugin showing a native kernel library built on Rack's own
DSP library (`rack::dsp`, in the Rack SDK's `include/dsp/`) rather than RackPy's
built-in kernels: a polyphonic biquad filter. It covers no single
manual sample. It is the RackPy counterpart of writing a C++ module's
`process()` with `rack::dsp`, and of checking it. No manual code is copied
([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project; rackpy-plugin.json lists the kernel library
res/Filter.svg                                   the original panel, with its components layer
kernels/dsp.cpp                                  the kernel library: dsp.Biquad
pythonpath/rackpydsp/__init__.py                 the entry package: adds the model
pythonpath/rackpydsp/filter.py                   the module and its widget
pythonpath/rackpydsp_teaching.py                 the same filter run from Python, offline (teaching only)
```

## The Filter

CUTOFF (20 Hz to 20.48 kHz on an exponential knob), RESONANCE (the filter's
Q) and MODE (lowpass, highpass, bandpass or notch) shape a polyphonic input.
The kernel, `dsp.Biquad`:

- runs `rack::dsp::TBiquadFilter<float_4>`, four channels per filter: one
  output channel per input channel, up to sixteen, and one silent channel
  when the input is unpatched;
- recomputes the coefficients every 32 frames with a `rack::dsp::ClockDivider`,
  as Rack's modules do to save the trigonometry, and at once after Initialize
  or a sample-rate change;
- runs on Rack's engine thread: no Python, allocation, lock or IO.

When bypassed, Rack copies the input to the output (`config_bypass`).

`rackpydsp_teaching.run` does the same with Rack's filter classes called from
Python (`rackpy.dsp.filters.TBiquadFilter[float_4]`, `ClockDivider`), one frame
at a time. **It is for teaching and tests only: not real-time, and it needs
Rack, which owns those classes.**

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/dsp --bundle /tmp/rackpy-bundle/RackPy \
  --kernels build/dev/examples/dsp/kernels --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` says `Loaded plugin RackPyDsp 2.0.0`. Patch an oscillator
through the Filter. Only one RackPy plugin runs in a Rack process, so remove
other RackPy plugins first, and remove `plugins-lin-x64/RackPyDsp` before
building again.

## What the tests check

`tests/examples/test_dsp_expanders.py`:

- in Rack (test-hooks build), the module made from the model against
  `rackpydsp_teaching.run` over five scenarios (unpatched; mono with the
  cutoff moved mid-run; five, sixteen and sixteen voices in highpass, bandpass
  and notch): the same channel counts, and samples within 1e-3 V. Measured:
  0 V in two scenarios, at most 3.0e-4 V. The filter code is Rack's in both,
  but RackPy and the kernel library are compiled separately with the SDK's
  `-funsafe-math-optimizations`, and the resonant notch amplifies the
  difference in rounding;
- it filters: at a 5.1 kHz cutoff a 200 Hz tone keeps its 5 V peak (within
  0.1 V); at 160 Hz a 5 kHz tone stays under 0.05 V;
- the kernel library references no CPython symbol, and `dsp.Biquad` makes no
  heap, lock, write or GIL call over 20 000 frames of changing polyphonic
  input, with its knobs moving and a reset and sample-rate change halfway,
  under RackPy's audio guard;
- built on a production bundle with its kernel library, the plugin loads
  under its own slug and keeps its module over two launches.

## Restrictions

Linux x86_64 and Rack Free 2.6.6 only, so far; the kernel library is native
code for one platform. Only one RackPy plugin can run in a Rack process. The
plugin's Python and C++ are trusted code, not a sandbox. Do not distribute
development builds. See [the template](../template/README.md#restrictions).
