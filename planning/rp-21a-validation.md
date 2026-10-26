# RP-21A validation

Date 2026-10-26, on `main` after 3d203d4. Pure Python: no SDK, compiler or
Rack is involved, and nothing here runs a graph natively (RP-21B).

RP-21A (`tk-rackpy-21a-graph-model-validation-and-the-offlin-1c0f89`) adds
`rackpy.dsp.graph` (the graph model, validation and lowering to a plan),
`rackpy.dsp.graph_reference` (the float32 offline reference) and the how-to
page [docs/how-to/graphs.md](../docs/how-to/graphs.md). Decisions are in
[rp-21-slices.md](rp-21-slices.md).

| Command | Result |
| --- | --- |
| `uv run pytest -q tests/dsp/test_graph.py` | 13 passed |
| `uv run pytest -q tests/docs tests/dsp/test_graph.py tests/unit -m "not host"` | 54 passed; the `graph_offline.py` snippet prints its expected output |
| `uv run mypy --strict src/rackpy/dsp tests/dsp/test_graph.py`, `uv run ruff check` | pass |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

`tests/dsp/test_graph.py` checks, against values worked out by hand from
Rack's code:

- Rack's tutorial oscillator over 200 frames, transcribed in float32, and a
  light blinking at 1 Hz.
- A VCA and polyphony: mono broadcast, missing channels of a shorter
  operand reading 0 V, unpatched inputs having no channels, outputs having
  at least one (`Port::setChannels`), and channel counts following the
  inputs frame by frame.
- Feedback through a delay (a counter, and a one-pole lowpass written with
  a forward reference), and a delay keeping the channels it was fed.
- `dsp::SchmittTrigger` (including its `UNINITIALIZED` start) and
  `dsp::PulseGenerator` (high before stepping).
- Controls and sample-rate changes applying from their frame.
- IEEE division by zero and NaN, `fmin`/`fmax`, `clamp`, `where` and the
  comparisons.
- Lowering: execution order, pruning of unused nodes and controls,
  latches, plain-data form.
- Refusals: cycles without a delay (with the path and the lines that made
  each node), unfed delays, unbound and circular forward references,
  duplicate outputs, lights and controls, signals from another graph,
  non-numeric operands and functions ("not compiled"), branching on a
  signal, `math.sin(signal)`, bad indices and non-finite constants, and the
  reference's own argument checks.

Nothing native was built or run for this slice.
