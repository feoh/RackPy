# Drawing

An original RackPy plugin with the features of the Plugin API Guide's
[custom widgets](https://vcvrack.com/manual/PluginGuide#Custom-widgets)
section and its text, self-illuminating layer and framebuffer parts, and of
the migration guide's notes on fonts and full-brightness drawing. No manual
code is copied ([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).
The [walkthrough](../../docs/how-to/draw-panels.md) explains it with
screenshots.

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project (bin/rackpy-plugin create)
res/Meter.svg                                    the original panel; its yellow rectangles are the custom widgets
pythonpath/rackpydrawing/__init__.py             the entry package: adds the model
pythonpath/rackpydrawing/meter.py                the module, its widget and four custom widgets
```

## The Meter

A voltmeter: SIGNAL's voltage is shown as text and as a bar on a scale of
±5 V or ±10 V (RANGE), and passed to THROUGH by a native kernel. Its panel has
four custom widgets, each a different way to draw:

| Widget | Draws | How |
| --- | --- | --- |
| `Shapes` | a rounded rectangle, a filled amber circle and a red-stroked triangle | NanoVG paths through `rackpy.raw.nanovg`, in `draw(args)` |
| `Readout` | the voltage as text, such as `+2.50 V` | Rack's ShareTechMono font, loaded from Rack's window every frame |
| `Bar` | a green bar from the centre towards the voltage | `draw_layer(args, 1)`, the self-lit layer, through `rackpy.drawing.Painter` |
| `Scale` | tick marks, one per volt | a `FramebufferWidget` around `Ticks`, marked dirty when RANGE changes |

The widgets read the input's voltage on Rack's UI thread when they draw, as
C++ widgets do; nothing in Python runs per sample.

- **Fonts.** `Readout` loads the font on every draw: Rack caches it, and a
  font kept across frames goes stale when Rack replaces its graphics context.
  If Rack cannot load it (`load_font` returns `None`), the readout draws
  nothing. The font is Rack's own, from Rack's system folder; this plugin
  ships none.
- **Layers.** Rack draws a panel's layer 0, dims it by the room brightness,
  then draws layer 1. The bar is on layer 1, so it glows in a dark room; the
  shapes, text and ticks dim.
- **Framebuffers.** Rack renders `Scale`'s children once into an image and
  draws the image every frame after. `Scale.step` calls `set_dirty()` when
  RANGE changes, so Rack renders the new ticks once.
- **The browser.** The preview's widget has no module. The widgets then draw
  `+0.00 V`, the 5 V scale and no bar.

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/drawing --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` says `Loaded plugin RackPyDrawing 2.0.0`. Patch any
signal into SIGNAL. Only one RackPy plugin runs in a Rack process, so remove
other RackPy plugins first, and remove `plugins-lin-x64/RackPyDrawing` before
building again.

## What the tests check

`tests/examples/test_drawing.py`, over the frames Rack Free draws under Xvfb
with Mesa's software OpenGL, with the dark-panels example's Lantern cabled
into a Meter at 10 V:

- the shapes' colours where they are drawn, the text's pixels, and the bar;
- at 25% room brightness the shapes dim to less than half while the bar's
  pixels stay within 5%;
- the scale renders once, not at all in the next ten frames, and exactly once
  after RANGE changes;
- with the font path pointing at a missing file the readout draws no text and
  nothing raises; with it restored, the text returns;
- after Rack's graphics-context destroy and create events, the scale renders
  again and the text is drawn with the reloaded font;
- deleting the Meter while Rack draws it, and undoing that, crashes nothing,
  and the restored Meter draws its text;
- a browser preview, with no module, renders;
- screenshots of the Meter, in a dark room and as a preview match reviewed
  references (`docs/_static/examples/`);
- built on a production bundle, the plugin loads under its own slug and its
  module survives two launches.

## Restrictions

Linux x86_64, Rack Free 2.6.6 and Mesa's software renderer only, so far: other
GPUs, drivers and platforms are not tested. Rack Free never recreates its
window, so recreation is tested by sending Rack's events through the scene.
Only one RackPy plugin can run in a Rack process. The plugin's Python is
trusted code, not a sandbox. Do not distribute development builds. See
[the template](../template/README.md#restrictions).
