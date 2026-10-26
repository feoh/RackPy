# App controls

`rackpy.raw.app` is `rack::app`: module widgets, Rack's param, port and
light widgets, their SVG versions, screws, panels and rails.
**Implemented (RP-28A, RP-28B), Linux x86_64 inside Rack Free 2.6.6**, for
the records of those headers and of `helpers.hpp`'s widget factories (see
the [coverage report](coverage.md)). The component library, the rack, the
scene, cables and displays come with the rest of RP-28.

Every class is Rack's own and is a [subclassable widget](widgets.md): create
it on Rack's main thread once the UI exists, set its fields, add it to a
parent, and override its drawing and event methods in Python if you need to.
Calling a method such as `on_drag_move(event)` runs Rack's implementation for
that class.

(app-module-widgets)=
## Module widgets

A model's module widget is a Python `ModuleWidget`. Rack asks the model's
*widget factory* for one whenever it shows the module:

- when it is added from the module browser;
- when a patch loads;
- when a deletion is undone or the module is duplicated;
- for the browser's preview, with no module.

```{rackpy-example} module_widget.py
```

`add_model(slug, module, widget=...)` takes the factory: a `ModuleWidget`
subclass, or a function, that Rack calls with the module or `None` (the raw
form is `create_model(..., widget_factory=)`). As in Rack's C++, the widget
calls `set_module(module)` first. The widget then *owns* the module: Rack
deletes the module with it. So `set_module` takes only the module Rack
passed the factory, once, and nothing replaces or drops it. Rack then sets
the widget's `model` and takes the widget.

If the factory raises, or returns something that is not a new
`ModuleWidget` with that module, RackPy logs the error. It leaves the module
untouched, and Rack gets RackPy's plain panel instead. The ports a factory
creates take part in cables (see [Ports](#app-ports)).

- `set_panel(panel)` takes a Python widget or a loaded `Svg` (Rack makes the
  `SvgPanel`). It replaces and deletes the previous panel, and snaps the
  widget's width to the rack grid. `get_panel()` returns the panel.
- `add_param`, `add_input` and `add_output` add controls. Rack's checks
  (port type, a duplicate input or output ID) raise `ValueError`.
  `get_param(id)`, `get_input(id)`, `get_output(id)`, `get_params()`,
  `get_ports()`, `get_inputs()` and `get_outputs()` search the widget's
  descendants.
- `to_json()`, `from_json(data)`, `save(path)`, `load(path)`,
  `save_template()`, `load_template()`, `has_template()`, `clear_template()`
  and `copy_clipboard()` need the widget's module (and model, for
  templates).
- `paste_json_action`, `paste_clipboard_action`, `load_action`,
  `reset_action`, `randomize_action`, `bypass_action`, `clone_action`,
  `disconnect_action`, `disconnect`, `remove_action` and
  `create_context_menu` need the widget in Rack's rack. They are Rack's menu
  actions, with undo steps. `remove_action` deletes the widget (the Python
  object is stale afterwards) and, like the other actions that change the
  widget tree, is refused inside a widget callback.
- `load_dialog`, `save_dialog`, `save_template_dialog` and
  `clear_template_dialog` open Rack's dialogs and wait for the user.
- `append_context_menu(menu)` is called by Rack at the end of its module
  context menu, so a subclass may add items. The menu is closed if the
  widget dies.
- `get_grid_position()`, `set_grid_position(pos)`, `get_grid_size()` and
  `get_grid_box()` are rack grid coordinates.
- `ModuleWidget(module)` is Rack's deprecated constructor, and warns.

Calls that take Rack's engine lock (the JSON, preset and clipboard calls,
and the actions) release the GIL while they run. Closing a Python-owned
module widget deletes its module, as Rack's destructor does.

`Model.create_module()` and `Model.create_module_widget(module=None)` are
Rack's calls for a RackPy model. Python owns what they make, and a widget
made for a module owns that module, which must be in Rack's engine and must
have no other widget. Other plugins' models need RP-24's handles for their
modules.

(app-controls)=
## Params

- `ParamWidget` is a control of one module parameter: `module` and
  `param_id`, `get_param_quantity()`, `reset_action()` (Rack's reset, with
  an undo step), the tooltip and `create_context_menu()`.
- `Knob` adds Rack's drag behaviour, with `horizontal`, `smooth`, `snap`,
  `speed`, `force_linear`, `min_angle` and `max_angle`. `SliderKnob` is the
  slider version.
- `Switch` steps through its values on each click. With `momentary` set, it
  holds the maximum while pressed.
- `init_param_quantity()` is Rack's per-kind set-up of the parameter's
  quantity: a `Knob` turns smoothing on unless it snaps, and a `Switch` turns
  snapping on and smoothing off. Override it in a subclass, and Rack's
  factories call it, as in C++.
- `append_context_menu(menu)` is called by Rack at the end of
  `create_context_menu()` with the menu it is building. A subclass may add
  items to that menu, for example with
  [`create_menu_item`](ui.md#menu-helpers).

(app-modules)=
## Controls and their module

A control's `module` is a RackPy module in Rack's engine, or `None`, as in a
module browser preview. Rack's history and cables find modules by ID, so a
module Python still owns is refused (`OwnershipError`). So is a module whose
constructor has not returned.

Rack indexes the module's parameters, ports and lights with a control's IDs
without checking them, so RackPy checks them. `param_id` must name one of
the module's parameters whenever a module is set. Set the ID first, then the
module, as Rack's factories do. A `PortWidget`'s `type` and `port_id`, and a
light's `first_light_id` and colours, are checked the same way.

When Rack deletes the module, RackPy sets the control's `module` back to
`None`. When a control is deleted, RackPy also releases Rack's
`touchedParam` (the param MIDI-Map would learn) and closes the control's
open context menu, since Rack's menu items point at the control.

(app-ports)=
## Ports

`PortWidget` has `module`, `type` (`rackpy.raw.engine.Port.Type`) and
`port_id`; `get_port()` and `get_port_info()` return the module's port and
its metadata. `delete_top_cable_action()` and the context menu are Rack's.

A Python port takes part in cables only when its module widget's factory
created it, and only while it is inside that widget. Rack keeps two
invariants for cabled ports:

- A port lives inside its module's widget, so removing the widget
  disconnects its cables.
- The module widget creates its ports itself, because Rack's undo and patch
  loading find ports through the widget and fail if one is missing.

A port added any other way still runs its Python drag overrides, but not
Rack's cable behaviour.

(app-lights)=
## Lights

- `LightWidget` draws `bg_color`, `color` and `border_color`.
  `draw_background(args)` runs on Rack's panel layer, and `draw_light(args)`
  and `draw_halo(args)` on its self-lit layer 1
  ([layers](window.md#frames-framebuffers-and-layers)). A subclass may
  override any of them.
- `MultiLightWidget` mixes `base_colors` by brightness: `add_base_color()`,
  `get_num_colors()`, and `set_brightnesses()`, which needs one brightness
  per colour.
- `ModuleLightWidget` follows the module's lights `first_light_id` onward,
  one per colour, each frame (Rack takes the square root of each
  brightness). It also has `get_light(color_id)`, `get_light_info()` and a
  tooltip.

(app-svg)=
## SVG controls, screws and panels

`SvgKnob`, `SvgSlider`, `SvgSwitch`, `SvgButton`, `SvgPort`, `SvgScrew` and
`SvgPanel` draw SVGs and cache them in a framebuffer. The `ThemedSvgPort`,
`ThemedSvgScrew` and `ThemedSvgPanel` versions take a light and a dark SVG,
and show the dark one while Rack prefers dark panels. Each has Rack's
setters:

- `set_svg`;
- `set_background_svg`, `set_handle_svg` and `set_handle_pos(_centered)` for
  sliders;
- `add_frame` and `frames` for switches and buttons;
- `set_background` for panels.

`SVGKnob`, `SVGPanel` and the other `SVG*` names are Rack's deprecated
spellings, and using them warns.

An SVG handed to any of them must be `None` or loaded: Rack draws these
widgets itself, and its `svgDraw` would crash on an `Svg` without an image.
For the same reason, `Svg.load_file()` keeps the previous image when it fails
([SVGs](window.md#svgs)).

`CircularShadow` (`blur_radius`, `opacity`), `PanelBorder` and `RailWidget`
are Rack's too. The constants `RACK_GRID_WIDTH` (15), `RACK_GRID_HEIGHT`
(380), `RACK_GRID_SIZE` and `RACK_OFFSET` are Rack's rack grid.

(app-host-views)=
## Rack's child widgets

An SVG control's parts are widgets Rack creates in its constructor and
deletes with it:

- `fb`, the framebuffer cache;
- `sw`, the `SvgWidget`;
- `tw`, the knob's rotation;
- `shadow`;
- a slider's `background` and `handle`;
- a panel's `panel_border`.

They are typed host views, and stale once the control is deleted:

- `rackpy.raw.widget.HostFramebufferWidget`: `dirty`, `bypassed`,
  `oversample` (finite, above 0 and at most 16, because Rack renders it
  unchecked), `set_dirty()`.
- `HostSvgWidget`: `svg`, `set_svg()`, `wrap()`.
- `HostTransformWidget`: `transform` as a 6-tuple, `identity()`,
  `translate()`, `scale()`, `rotate()`.
- `HostCircularShadow`: `blur_radius` and `opacity`.

Like any `HostWidget`, they cannot be moved or deleted from Python.

(app-factories)=
## Factories

`helpers.hpp`'s widget templates are `rackpy.raw` functions. The template
parameter is the first argument, a class (Python subclasses work), and each
sets Rack's fields in Rack's order:

- `create_widget(cls, pos)` and `create_widget_centered(cls, pos)`;
- `create_panel(svg_path)`, or `create_panel(light_path, dark_path)` for a
  `ThemedSvgPanel` (`panel_type=` chooses the class);
- `create_param(cls, pos, module, param_id)`, which calls
  `init_param_quantity()`, and `create_param_centered(...)`;
- `create_input(...)`, `create_output(...)` and their `_centered` versions;
- `create_light(cls, pos, module, first_light_id)` and
  `create_light_centered(...)`.

`createLightParam` comes with the component library's light buttons.
