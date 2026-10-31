# Draw a module's panel

**Implemented (RP-35), Linux x86_64 inside Rack Free {{rack_version}}.** This
walkthrough follows two example plugins, the
[Lantern](https://github.com/feoh/RackPy/tree/main/examples/dark-panels) and
the [Meter](https://github.com/feoh/RackPy/tree/main/examples/drawing). Each
section covers one feature of the Plugin API Guide's
[dark panels](https://vcvrack.com/manual/PluginGuide#Dark-panels) and
[custom widgets](https://vcvrack.com/manual/PluginGuide#Custom-widgets)
sections. The pictures are screenshots of Rack's own window, read back from
the frames Rack drew under Xvfb with Mesa's software OpenGL. The tests take
them again on every run and compare them with these, so they show what the
code does. Other GPUs and platforms are not tested yet.

The drawing reference is [Drawing and the window](../reference/window.md);
widgets are in [subclassable widgets](../reference/widgets.md).

## A light and a dark panel

Give `raw.create_panel` two SVGs, and it makes a `ThemedSvgPanel`, which shows
the second while Rack's **View > Use dark panels if available** is on. Rack's
themed screws and jacks (`ThemedScrew`, `ThemedPJ301MPort`) follow the same
setting:

```{rackpy-example-source} dark-panels/pythonpath/rackpydarkpanels/lantern.py
:pyobject: LanternWidget
```

Both SVGs must have the same size. Only the light one needs the components
layer that `bin/rackpy-plugin add-module` reads.

```{image} ../_static/examples/lantern-light.png
:alt: The Lantern on its light panel. A cream panel 6 HP wide with silver screws, a triangle at the top, a black knob in a dark ring, a glowing yellow lamp, and an output jack with an orange cable plugged in.
```
```{image} ../_static/examples/lantern-dark.png
:alt: The same Lantern with dark panels preferred. The panel is charcoal and its markings are amber; the screws and the jack are Rack's dark versions; the knob and the lamp are unchanged.
```

*Left: the light panel (cream, `#e9e3d5`). Right: the same module after
switching to dark panels. The panel is charcoal (`#1d2026`), the triangle
and ring are amber, and Rack swaps in its dark screws and jack. The knob and
lamp do not change.*

## Lights stay lit in a dark room

**View > Room brightness** dims everything Rack draws on a module's panel
layer. Rack then draws lights on a second, self-illuminating layer, which it
does not dim:

```{image} ../_static/examples/lantern-dark-room.png
:alt: The dark Lantern in a room at 25% brightness. The panel, knob and jack are nearly black, while the yellow lamp and the green light of the cable plug still glow at full brightness.
```

*At 25% room brightness the dark panel is nearly black, but the lamp and the
cable plug's light are as bright as before. The tests measure it: the panel's
pixels drop to a quarter of their brightness, and the lamp's stay within 12
of 255.*

## Shapes with NanoVG

A custom widget overrides `draw(args)`. `args.vg` is the NanoVG context for
that call only. The Meter's top widget fills a rounded rectangle and a circle,
and strokes a triangle:

```{rackpy-example-source} drawing/pythonpath/rackpydrawing/meter.py
:pyobject: Shapes
```

The panel adds each custom widget as C++ does: create it, set its box, and add
it (`MeterWidget` in the same file). The manual's version of this sample says
its triangle has a blue stroke, but the code draws a red one; this example's
comment says red.

## Text, with the font loaded every frame

`Readout.draw` asks Rack's window for the font on every call. Rack caches
loaded fonts, so this is cheap. A font kept from an earlier frame stops
working once Rack replaces its graphics context. If the font cannot be loaded,
`load_font` returns `None` and the readout draws nothing:

```{rackpy-example-source} drawing/pythonpath/rackpydrawing/meter.py
:pyobject: Readout
```

The font is Rack's own ShareTechMono, from Rack's system folder at run time;
the example does not ship a copy.

## A self-lit bar

`Bar` draws only on layer 1, the layer Rack does not dim, through the
[`rackpy.drawing`](../reference/window.md#the-drawing-facade) facade:

```{rackpy-example-source} drawing/pythonpath/rackpydrawing/meter.py
:pyobject: Bar
```

## A cached scale

Drawing every tick on every frame is wasted work: they change only with the
RANGE switch. A `FramebufferWidget` renders its children once into an image
and draws that image until it is marked dirty. `Scale` marks itself dirty when
RANGE changes:

```{rackpy-example-source} drawing/pythonpath/rackpydrawing/meter.py
:pyobject: Scale
```

The tests count how often Rack draws the ticks: once to begin with, never
in the next ten frames, and exactly once more after RANGE changes. A new
graphics context renders it again.

```{image} ../_static/examples/meter.png
:alt: The Meter with 10 volts at its input. A dark blue-grey panel 12 HP wide. At the top, an amber circle and a red-outlined triangle on a rounded slate background. Below, the text +10.00 V in light blue. Below that, a scale of white ticks from -10 to +10 volts, with a bright green bar filling its right half. At the bottom, an input jack with the orange cable, a two-position switch, and an output jack.
```
```{image} ../_static/examples/meter-dark-room.png
:alt: The same Meter at 25% room brightness. The panel, shapes, text and ticks are dim, but the green bar is as bright as before.
```

*Left: the Meter reading the Lantern's 10 V output on its 10 V range. The
green bar fills the right half of the scale. Right: in a room at 25%, the
shapes, text and ticks dim with the panel, and the self-lit bar does not.*

## In the module browser

The browser builds each preview from a module widget with no module (`None`).
Rack's control factories accept `None`, and the Meter's widgets draw a resting
meter instead of reading a module:

```{image} ../_static/examples/meter-preview.png
:alt: The Meter's browser preview. The same panel with +0.00 V, the 5 volt scale's eleven ticks and no bar; both jacks are empty.
```

*The preview shows +0.00 V, the default 5 V range's eleven ticks and no bar.*

## What else the tests check

`tests/examples/test_drawing.py` also deletes the Meter while Rack draws it
and undoes the deletion. It sends Rack's graphics-context destroy and create
events, as a host that recreates its window would, and points the readout at
a missing font. None of these crashes or raises, and the text comes back.
Each example also builds as its own plugin and loads in Rack.
