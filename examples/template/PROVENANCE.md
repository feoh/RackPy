# Provenance: plugin template

## Sources covered

The sample IDs are `upstream/examples.json`'s, each with the content hash
recorded there and a disposition naming its RackPy equivalent and tests.

| Sample | Feature | RackPy equivalent |
| --- | --- | --- |
| `tutorial-01` | Point `RACK_DIR` at the SDK | `bin/rackpy-dev build`, which uses the pinned, checksummed SDK; no variable to set |
| `tutorial-02`, `tutorial-03` | `helper.py createplugin` and its prompts | `bin/rackpy-plugin create`, with options instead of prompts |
| `tutorial-04`, `tutorial-05`, `panel-01` | `helper.py createmodule` from an SVG panel, and its prompts | `bin/rackpy-plugin add-module --panel` |
| `helper-createplugin-makefile` | `Makefile` including `plugin.mk` | `bin/rackpy-plugin build`: a RackPy bundle plus the project |
| `helper-createplugin-plugin-hpp`, `-plugin-cpp` | `pluginInstance` and `init()` | `pythonpath/rackpytemplate/__init__.py` and `rackpy.plugin.plugin` |
| `helper-createplugin-gitignore` | `.gitignore` | `.gitignore`, for Python's caches and `dist/` |
| `helper-createplugin-manifest` | `plugin.json` fields | the same fields, from `create`'s options |
| `helper-createmodule-manifest-entry` | the module's `plugin.json` entry | the same entry, from `add-module`'s options |
| `helper-createmodule-panel-parser` | the panel's `components` layer | `panel_components()` in `bin/rackpy-plugin` |
| `helper-createmodule-source` | the generated module and widget | `module_source()`: `pythonpath/rackpytemplate/starter.py` |

## Derived GPL source

`bin/rackpy-plugin`'s panel parsing and the shape of the module it generates
follow Rack's `helper.py` (Rack 2.6.6, commit
`061ccf63c1758599396ac1bb10d47345d9d34076`, sha256
`5d9f058e57e7b28c6fbbe3cfb4bbeb7e035492a7ecb1a6c46eff09348ff1fa12`,
Copyright VCV, GPL-3.0-or-later), which ADR 001 allows with attribution. They
are rewritten in Python for RackPy's API; no VCV manual text or code is used.

Intentional changes from `helper.py`:

1. Options instead of interactive prompts, so the workflow can be scripted and
   tested. `--license` has no default: helper.py's default, `proprietary`,
   cannot describe a plugin that contains GPL code.
2. The plugin is Python: the entry package replaces `plugin.hpp` and
   `plugin.cpp`, and `build` (a RackPy bundle plus the project) replaces the
   `Makefile`. `add-module` adds the model to the entry package itself, where
   helper.py prints what to add.
3. A rectangle's width and height are scaled without subtracting the view
   box's origin; helper.py subtracts it, which is wrong for a size and only
   harmless when the origin is 0. The template's own test checks this.
4. Component labels become valid Python names (`UNNAMED_0` for an unlabelled
   one, a `C_` prefix for one starting with a digit or naming a keyword), and
   two components of one kind with the same name are refused; helper.py
   generates C++ that would not compile.
5. Each custom widget class becomes a `rackpy.raw.widget.Widget` subclass in
   the generated module, and a rectangle's size is applied to it; helper.py
   only writes the size in a comment.
6. The panel must be inside the project, and the generated code loads it from
   where it is; helper.py assumes `res/<slug>.svg`.
7. Long lines are wrapped as `ruff format` wraps them, so generated code
   passes the repository's hooks.

## Original assets

`res/Starter.svg` is original RackPy artwork (GPL-3.0-or-later), not derived
from VCV's or Fundamental's panels. The widgets drawn on it at runtime are
Rack's component library, loaded from Rack's own resources; none of their
artwork is copied into this repository.
