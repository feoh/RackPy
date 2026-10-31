# Provenance: expanders

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `guide-13` | Access to a neighbour's params and ports | `expanders.Peek` in `kernels/expanders.cpp`, the Peeker's kernel |
| `guide-14` | Message buffers set in the constructor | `graph_messages(module.left_expander, 2)` in `receiver.py` |
| `guide-15` | Write the producer message and request the flip | the Source's `expander_send` lanes |
| `guide-16` | Read the consumer message | the Receiver's `expander_receive` lanes |

The content hashes are recorded with the samples in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Their dispositions there name these files and their tests.

## How it was made

The project and the first `source.py`, `receiver.py` and `peeker.py` were
generated with `bin/rackpy-plugin create` and `add-module` from the panels
(see [the template](../template/PROVENANCE.md) for that tool's provenance), then
edited by hand. `kernels/expanders.cpp` and `rackpyexpanders_graphs.py` are new.

This is an original example demonstrating the guide's features, written from
the pinned Rack headers and RackPy's API. RackPy's typed `"rackpy.graph"`
messages replace the manual's raw buffers, which Python cannot check. No VCV
manual code or text is copied or adapted
([ADR 001](../../docs/decisions/001-licensing-and-identity.md), option C).

## Original assets

`res/Source.svg`, `res/Receiver.svg` and `res/Peeker.svg` are original RackPy
artwork (GPL-3.0-or-later), not derived from VCV's or Fundamental's panels. The
knob, lights, jacks and screws are Rack's component library, drawn from
Rack's own resources at runtime.
