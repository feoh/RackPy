# UI widgets and menus

`rackpy.raw.ui` is `rack::ui`. **Implemented (RP-26A, RP-26B), Linux x86_64
inside Rack Free 2.6.6**, for every record in `include/ui/`, the menu helpers
and Rack's input dispatcher (see the [coverage report](coverage.md)).

(ui-widgets)=
## Widgets

Every class is Rack's own and is a [subclassable widget](widgets.md): create
it on Rack's main thread once the UI exists, configure its fields, add it to a
parent (which then owns it), and override its drawing and event methods in
Python if you need to. Calling a method such as `on_drag_start(event)` runs
Rack's implementation for that class.

- `Label`: `text`, `font_size`, `line_height`, `color` (an `NVGcolor`) and
  `alignment` (`Label.LEFT_ALIGNMENT`, `CENTER_ALIGNMENT`,
  `RIGHT_ALIGNMENT`).
- `Button`, and its kinds `ChoiceButton`, `RadioButton` and `OptionButton`:
  `text` and `quantity`. `Slider` and `ProgressBar` show a `quantity`.
  Rack does not own these quantities, so RackPy keeps the
  `rackpy.raw.Quantity` you assign (for example a
  `rackpy.types.ValueQuantity`) alive for as long as the widget refers to it,
  and releases it when the widget is deleted.
- `Tooltip` (`text`), `TooltipOverlay`, `List`, and `SequentialLayout`, which
  arranges its children in its own `step()`: `orientation`
  (`HORIZONTAL_ORIENTATION`, `VERTICAL_ORIENTATION`), `alignment`
  (`LEFT`/`CENTER`/`RIGHT`, with Rack's aliases `TOP`/`MIDDLE`/`BOTTOM`),
  `wrap`, `margin` and `spacing`.

Fields that hold a `Vec` or `Rect` return copies: assign a new value to
change them.

(ui-text-fields)=
## Text fields

`TextField` (and `PasswordField`) has Rack's raw fields (`text`,
`placeholder`, `password`, `multiline`, `cursor`, `selection`) and editing
methods: `get_text()`, `set_text()`, `select_all()`, `get_selected_text()`,
`insert_text()`, the clipboard methods and the cursor movements. `cursor` and
`selection` are clamped to the text, because Rack indexes the text with them.
`prev_field` and `next_field`, where Tab moves the focus, take a widget RackPy
manages, or `None`; if that widget is deleted, the field becomes `None` so
Rack never follows a dangling pointer. Python may override
`get_text_position(mouse_pos)`. `create_context_menu()` opens Rack's
cut/copy/paste menu.

(ui-scrolling)=
## Scrolling

`ScrollWidget` creates its own `container` and scrollbars, which it owns;
they appear as `HostWidget`s. Put content in the container with
`scroll.container.add_child(widget)`, which hands the widget to it.
`offset`, `container_box` and `hide_scrollbars` are Rack's fields, and
`scroll_to()`, `get_scroll_offset()`, `get_container_offset_bound()`,
`get_handle_offset()`, `get_handle_size()` and `is_scrolling()` its methods.
`Scrollbar` (alias `ScrollBar`) has `vertical`.

(ui-menus)=
## Menus

A menu is a `MenuOverlay` that covers the scene and holds one or more `Menu`s
of `MenuItem`s, `MenuLabel`s and `MenuSeparator`s. A `MenuItem` subclass
overrides `on_action(event)`. Rack consumes the event and then closes the
menu; call `event.unconsume()` to keep it open. `do_action()` triggers the
item as a click does. For a submenu, override `create_child_menu()` and
return a new `Menu`. Rack asks when the pointer enters the item and shows the
menu beside it. Building the new menu there is allowed even though Rack is
dispatching an event, because nothing else can be iterating a tree that
exists only in Python. Returning something other than a `Menu` is logged and
gives Rack no menu. `Menu.set_child_menu(menu)` does the same by hand. It
hands the menu to Rack, which deletes the previous child menu. `child_menu`
and `active_entry` are read-only. So is `parent_menu`, which Rack 2.6.6 never
sets. `ColorDotMenuItem` adds a `color`.

(ui-menu-helpers)=
## Menu helpers

Rack's `helpers.hpp` templates are functions in `rackpy.raw`, which take
Python callables. The template parameter becomes a keyword (`menu_type`,
`item_type`, `label_type`), and a Python subclass is fine:

- `create_menu()` opens a menu at the mouse position, in a `MenuOverlay` in
  Rack's scene, which owns both.
- `create_menu_label(text)` makes a label.
- `create_menu_item(text, right_text="", action=None, disabled=False,
  always_consume=False)` makes an item. Without `action` it is a plain item.
  With it, choosing the item calls `action()`.
- `create_check_menu_item(text, right_text, checked, action, ...)` shows a
  check mark while `checked()` is true.
- `create_bool_menu_item(text, right_text, getter, setter, ...)` toggles a
  bool.
- `create_submenu_item(text, right_text, create_menu)` opens a submenu that
  `create_menu(menu)` fills.
- `create_index_submenu_item(text, labels, getter, setter, ...)` chooses an
  index from `labels`.
- `create_bool_ptr_menu_item(text, right_text, ptr)` and
  `create_index_ptr_submenu_item(text, labels, ptr)` take a `rackpy.raw.Ref`
  (or any object with a `value`) where C++ takes a pointer. `None` behaves
  like a null pointer.

The callables live as long as Rack's menu item. If one uses a module that was
deleted while the menu was open, it raises `StaleHandleError`, which RackPy
logs and contains, so Rack carries on. `Model.append_context_menu(menu)` adds
Rack's own items for a model ([plugins and models](plugin.md#models)).

(ui-input)=
## Context and input

`rackpy.raw.context_get()`, and Rack's `APP()` macro, return the Rack context
current on this thread: RackPy's bound context on Rack's main thread, `None`
on threads Python starts. So far its `event` attribute is bound: Rack's input
dispatcher, a `rackpy.raw.widget.EventState`.

- `handle_button`, `handle_hover`, `handle_leave`, `handle_scroll`,
  `handle_text`, `handle_key`, `handle_drop` and `handle_dirty` do what
  Rack's window does with GLFW's input. They dispatch through the widget
  tree, Python widgets' handlers included. They are refused inside another
  widget callback, where Rack may be iterating the tree.
- `set_hovered_widget`, `set_dragged_widget`, `set_drag_hovered_widget` and
  `set_selected_widget` change the focus and send Rack's enter, leave,
  select and drag events.
- `finalize_widget` makes Rack forget a widget.
- The raw fields (`hovered_widget`, `selected_widget`, and the rest) can be
  written with `None` or a live widget without sending events.
  `root_widget`, the scene, is read-only. Rack clears the fields when a
  widget leaves its tree or is deleted. `held_keys` is a copy of Rack's set.

`rackpy.raw.macros` has `RACK_MOD_CTRL`, `RACK_MOD_SHIFT`, `RACK_MOD_ALT`
(Ctrl is Command on macOS), their `*_NAME`s, `RACK_MOD_MASK` and
`RACK_HELD`. `rackpy.raw.event` keeps Rack's deprecated `rack::event`
aliases (`event.Action` is `widget.ActionEvent`, and so on), with a
`DeprecationWarning`.

(ui-theme)=
## Theme and symbols

`set_theme(bg, fg)` sets Rack's widget colours from two `NVGcolor`s, and
`refresh_theme()` restores the ones for the current settings.
`CHECKMARK_STRING` ("✔"), `CHECKMARK(cond)` and `RIGHT_ARROW` ("▸") are
Rack's macros.

Everything here needs Rack's main thread (`ThreadError` elsewhere) once its
UI exists, as all widgets do.
