# Dark panels

An original RackPy plugin with the feature of the Plugin API Guide's
[dark panels](https://vcvrack.com/manual/PluginGuide#Dark-panels) section: a
module with a light panel and a dark one, chosen by Rack's **View > Use dark
panels if available**. No manual code is copied
([ADR 001](../../docs/decisions/001-licensing-and-identity.md)). The
[walkthrough](../../docs/how-to/draw-panels.md) shows it in screenshots.

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project (bin/rackpy-plugin create)
res/Lantern.svg                                  the original light panel, with its components layer
res/Lantern-dark.svg                             the original dark panel, the same size
pythonpath/rackpydarkpanels/__init__.py          the entry package: adds the model
pythonpath/rackpydarkpanels/lantern.py           the module and its widget
```

## The Lantern

`LanternWidget` gives `raw.create_panel` both SVGs, which makes a
`ThemedSvgPanel`, and uses Rack's themed screws and output jack
(`ThemedScrew`, `ThemedPJ301MPort`), which switch with the same setting. Its
lamp is a light, which Rack draws on its self-illuminating layer, so it stays
bright when **View > Room brightness** dims the panel. GLOW sets the lamp's
brightness and LEVEL outputs it as 0 to 10 V, through a native kernel.

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/dark-panels --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` says `Loaded plugin RackPyDarkPanels 2.0.0`. Add a
Lantern, then toggle **View > Use dark panels if available** and move **View >
Room brightness**. Only one RackPy plugin runs in a Rack process, so remove
other RackPy plugins first, and remove `plugins-lin-x64/RackPyDarkPanels`
before building again.

## What the tests check

`tests/examples/test_drawing.py`, over the frames Rack Free draws under Xvfb
with Mesa's software OpenGL:

- the light panel's colour, then the dark panel's with dark panels preferred;
- at 25% room brightness the panel falls to a quarter of its brightness while
  the lamp stays within 12 of its full-room value;
- screenshots of the light panel, the dark panel and the dark panel in a dark
  room match reviewed references (`docs/_static/examples/`);
- both SVGs are original artwork of the same size;
- built on a production bundle, the plugin loads under its own slug and its
  module survives two launches.

## Restrictions

Linux x86_64, Rack Free 2.6.6 and Mesa's software renderer only, so far: other
GPUs, drivers and platforms are not tested. Only one RackPy plugin can run in
a Rack process. The plugin's Python is trusted code, not a sandbox. Do not
distribute development builds. See [the template](../template/README.md#restrictions).
