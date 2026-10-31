# Menus

An original RackPy plugin with the features of the Plugin API Guide's
[context menu](https://vcvrack.com/manual/PluginGuide#Context-menu) section:
items a module widget adds to the module's right-click menu, and what they do
when there is no module, or no longer one. No manual code is copied
([ADR 001](../../docs/decisions/001-licensing-and-identity.md)).

```text
plugin.json, rackpy-plugin.json, .gitignore      the plugin project (bin/rackpy-plugin create)
res/Switchboard.svg                              the original panel, with its components layer
pythonpath/rackpymenus/__init__.py               the entry package: adds the model
pythonpath/rackpymenus/switchboard.py            the module (a ModuleClass), its widget and menu
```

## The Switchboard

A pitch source with only an output and a light on its panel. Its pitch, in
volts per octave from middle C, comes from its context menu, where
`SwitchboardWidget.append_context_menu` adds, after Rack's own items:

| Item | Helper | Does |
| --- | --- | --- |
| a separator | `ui.MenuSeparator()` | |
| *Pitch* | `create_menu_label` | a heading |
| *Up a semitone*, *Down a semitone* | `create_menu_item` with an action | move the pitch; disabled at the ends of the range |
| *Middle C* | `create_check_menu_item` | checked at middle C; choosing it goes there |
| *Presets* | `create_submenu_item` | a submenu built when it opens: Middle C, A 440, Low C, High G |
| *Octave* | `create_index_submenu_item` | -2 to +2, through a getter and a setter |
| *Note* | `create_index_ptr_submenu_item` | C to B, through an object with a `value` |
| a separator | `ui.MenuSeparator()` | |
| *Muted* | `create_bool_menu_item` | 0 V, and the light on |
| *Inverted* | `create_bool_ptr_menu_item` | the pitch's negative |

A C++ "pointer" item holds a pointer to a field of the module. RackPy's takes
any object with a `value` attribute; `Choice` is one whose `value` is an
attribute of the module's state, and setting it also hands the kernel the new
voltage. The choices are saved in the patch (`to_json`), read back as
untrusted data (`from_json`), and Initialize restores middle C.

The kernel only outputs the voltage and lights the light, natively; the menu's
callbacks run on Rack's main thread when an item is chosen.

## No module, or no longer one

- Rack's module browser builds a module widget with no module (`None`) for
  its preview, and calls the widget class with it. The widget's constructor
  works without one (Rack's control factories take `None`), and
  `append_context_menu` adds nothing without a module.
- An item lives as long as its menu. If the module is deleted while the menu
  is open, the items still work: the state they change is the instance's own,
  and `Switchboard.send()` stops handing voltages to a kernel that is gone
  (`module.alive`). RackPy contains any error a menu callback raises, so a
  stale call cannot crash Rack, but this example raises none.

## Build and run

Linux x86_64 with uv, CMake, Ninja and GCC, and Rack Free 2.6.6. From the
repository root:

```bash
uv sync --frozen
bin/rackpy-dev build
bin/rackpy-dev bundle /tmp/rackpy-bundle
bin/rackpy-plugin build examples/menus --bundle /tmp/rackpy-bundle/RackPy --dest ~/.local/share/Rack2/plugins-lin-x64
```

Start Rack: `log.txt` says `Loaded plugin RackPyMenus 2.0.0`. Add a
Switchboard, patch its output to an oscillator's pitch input, and right-click
it. Only one RackPy plugin runs in a Rack process, so remove other RackPy
plugins first, and remove `plugins-lin-x64/RackPyMenus` before building again.

## What the tests check

`tests/examples/test_controls_state.py`:

- in Rack (test-hooks build): a browser preview's menu is empty; the module's
  menu has the label, the two separators and the eight items in order; each
  item does what the table says, and Rack's engine outputs the voltage it
  sets; the check mark and the index submenus' labels follow the state; the
  ends of the range disable the actions; the saved state round-trips and
  ignores invalid fields; Initialize restores middle C; after the module is
  deleted, choosing items,
  opening submenus and choosing from them raises nothing;
- in windowed Rack, a switchboard's state survives Rack's save and reopen;
- built on a production bundle, the plugin loads under its own slug and its
  module survives two launches.

## Restrictions

Linux x86_64 and Rack Free 2.6.6 only, so far. Only one RackPy plugin can run
in a Rack process. The plugin's Python is trusted code, not a sandbox. Do not
distribute development builds. See [the template](../template/README.md#restrictions).
