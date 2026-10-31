# Provenance: drawing

## Sources covered

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `guide-18` | Custom widget drawing with NanoVG | `Shapes.draw` |
| `guide-19` | Create, size and add a custom widget | `MeterWidget.__init__` |
| `guide-20` | Text drawing with a font | `Readout.draw` |
| `guide-21` | Self-illuminating layer with `drawLayer` | `Bar.draw_layer` |
| `guide-22` | Framebuffer caching with `FramebufferWidget` | `Scale` around `Ticks` |
| `guide-23` | Mark a framebuffer dirty | `Scale.step` |
| `migrate2-07` | Don't keep Font and Image references across frames | as `guide-20` |
| `migrate2-12` | Draw light-like widgets at full brightness | as `guide-21` |

The content hashes are recorded with the samples in `upstream/examples.json`
(`bin/manual-sources check` confirmed on 2026-10-31 that the manual's blocks
are unchanged). Their dispositions there name these files and their tests.

## Corrections of the manual

`guide-18`'s comment describes a triangle with a blue stroke, but its code
strokes it red. This example's triangle is stroked red, and its comment says
so ([errata](../../docs/licensing.md#errata-found-while-classifying)).

## How it was made

The project and the first `meter.py` were generated with `bin/rackpy-plugin
create` and `add-module` from `res/Meter.svg` (see
[the template](../template/PROVENANCE.md) for that tool's provenance); its
yellow rectangles labelled `name#Class` became the four widget classes. The
widgets' drawing, the scale's caching, the switch and the kernel were written
by hand.

This is an original example demonstrating the guide's features, written from
the pinned Rack headers and RackPy's API. No VCV manual code or text is copied
or adapted ([ADR 001](../../docs/decisions/001-licensing-and-identity.md),
option C).

## Assets

`res/Meter.svg` is original RackPy artwork (GPL-3.0-or-later), not derived
from VCV's or Fundamental's panels. The switch, jacks and screws are Rack's
component library, drawn from Rack's own resources at runtime. The readout's
font, ShareTechMono, is Rack's (SIL Open Font License 1.1); the plugin loads it
from Rack's system folder at run time and does not include it. The screenshots
in `docs/_static/examples/` are of this panel as Rack drew them
(GPL-3.0-or-later).
