# Plugin template

A RackPy plugin project exactly as `bin/rackpy-plugin` generates it: RackPy's
counterpart of the plugin that Rack's `helper.py createplugin` and
`createmodule` make ([tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial),
[panel guide](https://vcvrack.com/manual/Panel)). Its one module, *Starter*,
has a knob, a light, an input, an output and a custom widget, read from the
original panel's hidden `components` layer. It configures them and produces no
sound: add a kernel for that, as [the oscillator](../oscillator/README.md) does.

```text
plugin.json                         Rack's manifest (as createplugin's)
rackpy-plugin.json                  the entry package's name
pythonpath/rackpytemplate/__init__.py    the entry package (plugin.hpp/plugin.cpp's role)
pythonpath/rackpytemplate/starter.py     the module and its widget (createmodule's source)
res/Starter.svg                     the panel, with its components layer
.gitignore
```

## Requirements

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. RackPy is not
released: build it from this repository ([CONTRIBUTING.md](../../CONTRIBUTING.md)).
macOS and Windows are not supported yet (RP-40, RP-41).

## Make your own plugin

From the repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle          # prints /tmp/rackpy-bundle/RackPy
bin/rackpy-plugin create MyPlugin --license GPL-3.0-or-later --name "My Plugin" --author "Your Name"
```

Draw `MyPlugin/res/MyModule.svg`, with a layer named `components` (hide it)
holding a red circle or rectangle for each knob, green for each input, blue
for each output, magenta for each light and yellow for each custom widget. A
shape's label (`pitch`, or `pitch#RoundLargeBlackKnob` to choose the widget
class from `rackpy.raw.componentlibrary`) names it. Then:

```bash
bin/rackpy-plugin add-module MyModule --project MyPlugin --panel MyPlugin/res/MyModule.svg \
    --name "My Module" --description "What it does" --tags Utility
bin/rackpy-plugin build MyPlugin --bundle /tmp/rackpy-bundle/RackPy --dest /path/to/Rack2/plugins-lin-x64
```

`/path/to/Rack2` is your Rack user folder: on Linux, usually `~/.local/share/Rack2`.
Start Rack: its log (`log.txt` there) says `Loaded plugin MyPlugin 2.0.0`, and
the module browser lists *My Module*. Edit `pythonpath/myplugin/mymodule.py`
and run `build` again (remove the old plugin directory first).

## Regenerate this example

```bash
cd "$(mktemp -d)"
REPO=/path/to/RackPy
$REPO/bin/rackpy-plugin create RackPyTemplate template --license GPL-3.0-or-later --name "RackPy Template" \
    --brand RackPy --author "RackPy contributors" --source-url https://github.com/feoh/RackPy
cp $REPO/examples/template/res/Starter.svg template/res/
$REPO/bin/rackpy-plugin add-module Starter --project template --panel template/res/Starter.svg --name Starter \
    --description "A starting point: one of each component" --tags Utility
```

`tests/examples/test_template.py` runs exactly these steps and requires the
result to match this directory byte for byte, then builds it on a production
RackPy bundle and loads it in Rack under its own slug, `RackPyTemplate`.

## Restrictions

- **One RackPy plugin per Rack process.** Each one carries its own CPython;
  RackPy refuses to start a second ([ADR 002](../../docs/decisions/002-embedding-and-bindings.md)).
- **Trusted code.** The plugin runs its own Python with Rack's privileges. It
  is not a sandbox, and it never runs code from patches.
- **Licence.** The built plugin contains RackPy (GPL-3.0-or-later) and
  CPython; `--license` is required and must be compatible. How RackPy's
  documentation advises plugin authors on licensing is an open owner question
  ([licensing](../../docs/licensing.md)). This is not legal advice.
- **Development builds** need the glibc of the machine that built them, and
  the bundle's runtime is a symbolic link unless you bundle with
  `--runtime copy`. Do not distribute them.
- Python never runs per sample: give each module a native kernel.

The differences from `helper.py` are in [PROVENANCE.md](PROVENANCE.md).
