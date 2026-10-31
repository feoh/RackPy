# RP-36 validation: polyphony, SIMD, DSP and expander examples

Task: `tk-rackpy-36-port-polyphony-simd-dsp-and-expander-g-c9ec49`, branch `rp36`.
Machine: Intel i9-14900K, Linux 7.3.0-4-cachyos x86_64, GCC 16.2.1, Python 3.14,
Rack Free 2.6.6 (`061ccf63c1758599396ac1bb10d47345d9d34076`).

## What was delivered

- Example plugins can ship C++ kernel libraries: `examples/<project>/kernels/*.cpp`
  is built by RackPy's CMake with the SDK's plugin flags into
  `build/<dir>/examples/<project>/kernels/`, and `bin/rackpy-plugin build
  --kernels DIR` copies the libraries a project's `rackpy-plugin.json` lists
  and adds them to `rackpy-loader.json` (refusing a missing `--kernels` or
  library).
- `examples/polyphony/`: `polyphony.Scalar` (a `Voice` per channel) and
  `polyphony.Simd` (`float_4`, four channels per step) as two models; plain
  Python teaching loops (`rackpypolyphony_teaching`).
- `examples/dsp/`: `dsp.Biquad`, Rack's `TBiquadFilter<float_4>` with a
  `ClockDivider` for coefficients; `rackpydsp_teaching` runs the same Rack
  classes from Python.
- `examples/expanders/`: Source and Receiver graphs exchanging two typed
  `"rackpy.graph"` lanes; the Peeker's C++ kernel `expanders.Peek` reads only
  a same-plugin `Source` neighbour.
- `native/tests/polyphony_reference.cpp` (C++ reference) and
  `native/tests/example_kernel_guard.cpp` (every example kernel under the
  audio guard).
- `tests/examples/test_dsp_expanders.py`, `tests/examples/in_rack_dsp/`.
- Dispositions for all 11 RP-36 samples. The inventory audit
  (`tests/examples/test_dispositions.py`): every tutorial, guide and
  plugin-template sample has a tested disposition; the other 19 samples are
  12 for RP-37's guides (10 shell, 2 out-of-scope migration notes), 3 for
  RP-39's packaging (2 shell, 1 config), and 4 about building Rack itself,
  out of scope with a recorded reason. None is unassigned.

Manual corrections: `guide-25` and `guide-30` errata (whole-array `sin`,
`float_t`) are fixed in the example and noted in `docs/licensing.md`.
Fundamental is not used or presented as an official sample; `examples/dsp`
says it covers no manual sample.

## Evidence

Fresh `build/dev` (`--werror --test-hooks`) and `build/production`
(`--werror`), each `100% tests passed out of 7` (CTest).

| Check | Result |
| --- | --- |
| Full pytest (both builds, host and GUI) | 453 passed in 927.88 s |
| `tests/examples/test_dsp_expanders.py` | 12 passed (14 in Rack) |
| `prek run`, strict mypy, pyright facade contract, `sphinx-build -W -n` | clean |
| `bin/rackpy-dev audio-symbols` on the three example libraries | no CPython symbols |

Audio guard, 20 000 frames per kernel type (0 to 16 changing channels, moving
params, a reset and sample-rate change halfway; Peek with a Source beside it):
`dsp.Biquad`, `expanders.Peek`, `polyphony.Scalar` and `polyphony.Simd` each
made 0 malloc, free, calloc, realloc, aligned, mutex_lock, write and
gil_ensure calls.

Measured differences:

| Comparison | Largest difference |
| --- | --- |
| C++ reference, `float_4` vs scalar (5 scenarios, 1200 frames at 48 kHz) | 9.6e-7 to 1.28e-5 V |
| Python teaching loop vs scalar C++ | 1.24e-5 to 4.25e-4 V |
| PolySine and PolySineScalar in Rack vs their C++ reference (0, 1, 5, 16, 16→3 channels) | 5e-9 V |
| Filter kernel vs Rack's filter from Python (5 scenarios) | 0, 1.34e-5, 4.29e-6, 0, 3.01e-4 V (allowed 1e-3) |

The filter's differences come from compiling the same Rack code twice with
`-funsafe-math-optimizations` (float32 emulation of the cutoff did not change
them); the resonant notch amplifies them. A frame or wiring error would be
volts.

Expanders, in Rack's engine stepped by the test on 1, 2 and 4 threads: 40
frames with the Receiver equal to the Source's previous frame; follows a
newly adjacent Source after one flip; 0 V and a dark light at once when the
Source is removed. The Peeker reads only a Source, follows its knob, and reads
nothing beside a Receiver or after removal.

Each example builds with its kernel library on a production bundle and loads
under its own slug, keeping its modules over two launches.

## Limitations

- Linux x86_64 and Rack Free 2.6.6 only. Kernel libraries are native code;
  macOS and Windows builds of them are not made or tested.
- The kernel libraries are built by RackPy's CMake; a plugin author outside
  the repository builds them with the SDK flags described in
  `docs/how-to/dsp.md`, which is not automated by `bin/rackpy-plugin`.
- Engine-thread checks step Rack's engine from the test (`step_block`), as
  RP-23's do; they are not timing measurements of Rack's own thread.
