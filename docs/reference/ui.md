# UI widgets and menus

`rackpy.raw.ui` is `rack::ui`. **Implemented (RP-26A), Linux x86_64 inside
Rack Free 2.6.6**, for every record in `include/ui/` (see the
[coverage report](coverage.md)). Menu helper functions and Rack's input
dispatcher come with the rest of RP-26.

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

(ui-theme)=
## Theme and symbols

`set_theme(bg, fg)` sets Rack's widget colours from two `NVGcolor`s, and
`refresh_theme()` restores the ones for the current settings.
`CHECKMARK_STRING` ("✔"), `CHECKMARK(cond)` and `RIGHT_ARROW` ("▸") are
Rack's macros.

Everything here needs Rack's main thread (`ThreadError` elsewhere) once its
UI exists, as all widgets do.
