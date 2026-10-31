# Add items to a module's menu

Right-clicking a module opens its context menu: Rack's own items, then any
your module widget adds. This guide adds labels, actions, check marks,
submenus and settings, with Rack's menu helpers.

**Status:** Linux x86_64, Rack Free {{rack_version}}, tested in Rack
([examples/menus](../../examples/menus/README.md), whose module uses every
helper below).

## Override `append_context_menu`

Rack calls your module widget's `append_context_menu(menu)` each time the
menu opens, after adding its own items. Add yours to `menu`:

```{rackpy-example-source} menus/pythonpath/rackpymenus/switchboard.py
:pyobject: SwitchboardWidget
```

The helpers live in `rackpy.raw` and match Rack's C++ `createMenuItem` family:

| Helper | Makes |
| --- | --- |
| `ui.MenuSeparator()` | a line between groups |
| `create_menu_label(text)` | a heading that does nothing |
| `create_menu_item(text, right_text, action, disabled)` | an item that calls `action()` when chosen |
| `create_check_menu_item(text, right_text, checked, action)` | an item with a check mark while `checked()` is true |
| `create_bool_menu_item(text, right_text, getter, setter)` | a checkable setting through a getter and a setter |
| `create_bool_ptr_menu_item(text, right_text, ptr)` | the same, through `ptr`: any object with a `value` attribute |
| `create_submenu_item(text, right_text, create_menu)` | a submenu that `create_menu(submenu)` fills when it opens |
| `create_index_submenu_item(text, labels, getter, setter)` | a submenu that chooses one of `labels` by index |
| `create_index_ptr_submenu_item(text, labels, ptr)` | the same, through an object with a `value` |

Where C++ takes a pointer to a field, RackPy takes an object with a `value`
attribute: the example's `Choice` stores the value in the module's state and
hands the kernel the new voltage when it changes. The [ui reference](../reference/ui.md#menu-helpers)
gives each helper's exact signature.

## What to keep in mind

- **The browser's preview has no module.** Rack builds your widget with
  `None` for its module browser. Add no module items then, as the example's
  first line does.
- **Menu callbacks run on Rack's main thread**, when the user chooses an item.
  They may change the module's state and Python objects freely; to change what
  the audio does, set a kernel control (`kernel.set(...)`), which reaches the
  engine at the next sample.
- **An item can outlive its module.** If the module is deleted while its menu
  is open, a callback still runs when chosen. Check `module.alive` before
  using the module, as the example's `send()` does. RackPy contains any error
  a callback raises.
- **Save what the menu sets.** Settings chosen from the menu are module state:
  save them in `to_json` and restore them in `from_json`
  ([state](state.md)). The example does, and Initialize resets them.
