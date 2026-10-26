# App controls

`rackpy.raw.app` is `rack::app`: Rack's param, port and light widgets, their
SVG versions, screws, panels and rails. **Implemented (RP-28A), Linux x86_64
inside Rack Free 2.6.6**, for the records of those headers and of
`helpers.hpp`'s widget factories (see the [coverage report](coverage.md)).
Module widgets, the component library, the rack, the scene, cables and
displays come with the rest of RP-28.

Every class is Rack's own and is a [subclassable widget](widgets.md): create
it on Rack's main thread once the UI exists, set its fields, add it to a
parent, and override its drawing and event methods in Python if you need to.
Calling a method such as `on_drag_move(event)` runs Rack's implementation for
that class.

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

A Python port does not take part in cables yet. Its Python drag overrides
still run, but Rack's cable behaviour does not. Rack keeps two invariants for
cabled ports:

- A port lives inside its module's widget, so removing the widget
  disconnects its cables.
- The module widget creates its ports itself, because Rack's undo and patch
  loading find ports through the widget and fail if one is missing.

Python module widgets (RP-28B) create ports that cable normally.

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
