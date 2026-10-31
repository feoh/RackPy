# Provenance: dark-panels

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `guide-17` | Light and dark panel with `createPanel` | `raw.create_panel(light, dark)` in `LanternWidget`, with themed screws and jack |

The content hash is recorded with the sample in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Its disposition there names these files and their tests.

## How it was made

The project and the first `lantern.py` were generated with
`bin/rackpy-plugin create` and `add-module` from `res/Lantern.svg` (see
[the template](../template/PROVENANCE.md) for that tool's provenance), then
edited by hand: the themed panel, screws and jack, the lamp, the kernel and
the module docstring.

This is an original example demonstrating the guide's feature, written from
the pinned Rack headers and RackPy's API. No VCV manual code or text is copied
or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C).

## Original assets

`res/Lantern.svg` and `res/Lantern-dark.svg` are original RackPy artwork
(GPL-3.0-or-later), not derived from VCV's or Fundamental's panels. The knob,
light, jack and screws are Rack's component library, drawn from Rack's own
resources at runtime. The screenshots in `docs/_static/examples/` are of these
panels as Rack drew them (GPL-3.0-or-later).
