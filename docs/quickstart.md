# Quickstart: your first RackPy plugin

:::{admonition} Pre-release: from source, Linux only
:class: important

RackPy has no release. These steps build it from a checkout of the repository
and make a plugin that only the computer that built it can load. Do not
distribute what they make. macOS and Windows are not supported yet.
:::

By the end you will have a plugin of your own in Rack, with one module whose
panel you drew, and you will know where to look when something goes wrong.
It takes about 30 minutes, most of it the first build.

## What you need

- **Linux x86_64**, with a desktop to run Rack in.
- **VCV Rack Free {{rack_version}}**, from <https://vcvrack.com/Rack>. Start it
  once, so that it creates its user folder (usually `~/.local/share/Rack2`).
- **[uv](https://docs.astral.sh/uv/)**, **CMake**, **Ninja**, **GCC** and **git**.
  RackPy downloads everything else it builds with, pinned: the Rack SDK, its
  own Python ({{python_version}}) and its Python packages.
- An editor for SVG files, such as [Inkscape](https://inkscape.org/), to draw a
  panel. The first plugin can reuse one of the examples' panels.

You do not need to know C++, and you do not need Python installed: uv provides
it.

## 1. Build RackPy

```bash
git clone https://github.com/feoh/rackpy.git
cd rackpy
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
```

The build takes a few minutes the first time, while it downloads the SDK and
Python. The last command prints `/tmp/rackpy-bundle/RackPy`: RackPy itself,
ready to be combined with your plugin. Run it again only after rebuilding,
into an empty folder.

## 2. Create a plugin

```bash
bin/rackpy-plugin create MyPlugin --license GPL-3.0-or-later --name "My Plugin" --author "Your Name"
```

This makes `MyPlugin/`, a plugin project: Rack's manifest (`plugin.json`),
the Python package Rack imports when it loads the plugin
(`MyPlugin/pythonpath/myplugin/`), and `res/` for panels. A RackPy plugin
contains RackPy and CPython, so its licence must be compatible with
GPL-3.0-or-later ([licensing](licensing.md)).

## 3. Add a module from a panel

Rack modules are drawn as SVG panels. Copy the oscillator example's panel to
start with:

```bash
cp examples/oscillator/res/Oscillator.svg MyPlugin/res/MyModule.svg
bin/rackpy-plugin add-module MyModule --project MyPlugin --panel MyPlugin/res/MyModule.svg \
    --name "My Module" --description "My first module" --tags Oscillator
```

`add-module` reads the panel's hidden `components` layer, where each coloured
shape marks a control: red for a knob, green for an input, blue for an output,
magenta for a light. It writes `MyPlugin/pythonpath/myplugin/mymodule.py`, a
module with those controls and a widget that places them on the panel, and adds
the module to `plugin.json`. [The template](../examples/template/README.md)
describes the colours and labels in full.

## 4. Build the plugin into Rack

Quit Rack first: it reads plugins only when it starts. Then:

```bash
bin/rackpy-plugin build MyPlugin --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack. Its log, `~/.local/share/Rack2/log.txt`, now says
`Loaded plugin MyPlugin 2.0.0`, and the module browser lists *My Module*. Add
it to the rack: it has the panel's knob, ports and light, and makes no sound
yet, because it has no audio processing.

To change the module, edit `mymodule.py`, quit Rack, delete
`~/.local/share/Rack2/plugins-lin-x64/MyPlugin` and run `build` again.

## 5. Make a sound

A module's audio is processed natively, never by Python sample by sample.
Python describes the processing once, as a graph, and RackPy compiles it into
native code that Rack runs every sample. [The oscillator tutorial](tutorials/oscillator.md)
turns this module into a sine oscillator and explains each line.

## When something goes wrong

Rack keeps running when a RackPy plugin fails, and its log says why: look for
lines with `RackPy`. [Troubleshooting](troubleshooting.md) lists the messages
and what to do about each.

Only one RackPy plugin can run in Rack at a time: each carries its own Python,
and RackPy refuses to start a second. If you installed one of the examples,
remove it before adding your own.
