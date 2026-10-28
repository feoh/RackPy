# App controls

`rackpy.raw.app` is `rack::app`: module widgets, Rack's param, port and
light widgets, their SVG versions, screws, panels and rails.
**Implemented (RP-28A, RP-28B), Linux x86_64 inside Rack Free 2.6.6**, for
the records of those headers and of `helpers.hpp`'s widget factories (see
the [coverage report](coverage.md)), and for Rack's scene, rack, cables and
LED displays (RP-28D). The [component library](componentlibrary.md) is its
own page. Python-made cable widgets and the audio and MIDI displays come
with the rest of RP-28.

Python-created controls are Rack's own [subclassable widgets](widgets.md):
create them on Rack's main thread once the UI exists, set their fields, add
them to a parent, and override drawing and events in Python if needed.
The borrowed scene/rack/scroll/plug host views below are **not** subclass
trampolines; their callable wrappers do not supply Python virtual overrides.
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
  `disconnect_action`, `remove_action` and `create_context_menu` need the
  widget in Rack's rack. The modifying menu actions have undo steps.
  `remove_action` deletes the widget (the Python object is stale afterwards)
  and, like the other actions that change the widget tree, is refused inside
  a widget callback.
- `disconnect()` also needs the widget in Rack's rack, but removes its cables
  **without an undo step**. Use `disconnect_action()` for an undoable removal.
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
Rack's calls. Python owns what they make, and a widget made for a module
owns that module, which must be in Rack's engine, must have no other widget
and must have no cables that Python added to the engine. A RackPy model's
module is a `Module`, and its widget, with a widget factory, is a Python
`ModuleWidget`.

### Modules from other plugins

For another plugin's model (or a RackPy model without a widget factory),
the model's own code makes the module and the widget (**RP-24D**):

- `create_module()` returns an `EngineModule` that Python owns
  (`in_engine` false). `Engine.add_module(module)` gives it to Rack's
  engine, after which the handle finds it by ID. If Python drops it first,
  it is deleted on Rack's main thread, outside the engine lock: a module's
  destructor may use Rack's engine, as Core's MIDI-Map does.
- `create_module_widget(module)` returns a `ForeignModuleWidget` that
  Python owns: its `model`, its `module`, a writable `box` and `close()`.
  `HostRackWidget.add_module(widget)` or `add_module_at_mouse(widget)` hands
  it to Rack's rack, which refuses a widget that is not 3U high. Closing or
  dropping it deletes it and its module, on the main thread outside the
  engine lock. A preview widget (`module=None`) never enters the rack,
  because Rack indexes the rack's widgets by their module. The same rule now
  applies to Python module widgets.
- `Engine.remove_module` takes back another plugin's module only when no
  module widget owns it and no cables touch it. Python then owns it again.

(app-controls)=
## Params

- `ParamWidget` is a control of one module parameter: `module` and
  `param_id`, `get_param_quantity()`, `reset_action()` (Rack's reset, with
  an undo step), `create_tooltip()`, `destroy_tooltip()` and
  `create_context_menu()`. Rack's enter and leave handlers manage the tooltip
  on hover.
- `Knob` adds Rack's drag behaviour, with `horizontal`, `smooth`, `snap`,
  `speed`, `force_linear`, `min_angle` and `max_angle`. With knob scrolling
  enabled, Rack's `on_leave` commits a changed scroll value to history for
  undo. `SliderKnob` is the slider version.
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
`create_tooltip()` and `destroy_tooltip()` open and remove its tooltip;
Rack's `on_enter` and `on_leave` handlers do the same. Tooltips must be
enabled in Rack's settings, and the port needs a module.

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
  per colour. Reading `base_colors` returns a list copy, including copied
  colors; mutating it does not update Rack. Assign a list back to replace the
  colors, or use `add_base_color()`.
- `ModuleLightWidget` follows the module's lights `first_light_id` onward,
  one per colour, each frame (Rack takes the square root of each
  brightness). It also has `get_light(color_id)`, `get_light_info()` and a
  tooltip. `create_tooltip()` and `destroy_tooltip()` manage it directly;
  Rack's enter and leave handlers do so on hover.

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

The themed widgets expose `light_svg` and `dark_svg`. Sliders expose
`min_handle_pos` and `max_handle_pos`; the deprecated `set_background_svg_legacy`,
`set_handle_svg_legacy` and `set_svgs` spellings warn. `SvgKnob` and `SvgPort`
likewise retain a warning `set_svg_legacy` spelling.

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

- `create_light_param(cls, pos, module, param_id, first_light_id)` and
  `create_light_param_centered(...)` are for a control with a
  `get_light()`, such as the [component library](componentlibrary.md)'s
  light sliders and buttons. They set the light's module and first light
  ID too.

(app-rack)=
## The scene and the rack

Rack makes and owns its `Scene`, `RackScrollWidget` and `RackWidget`, so
Python reaches them as host views (stale once Rack deletes them). Their
constructors and destructors are PRIVATE, so no plugin, in C++ or Python,
can subclass them, and the same holds in effect for `PlugWidget` (see
[Rack's cables](#app-cables)). Their UI virtuals (`step`, `draw` and the
event handlers) are therefore calls on Rack's own instances, through the
host views (an RP-28F decision the owner approved on 2026-10-26; [coverage
policy 11](../coverage.md#completeness-policy)). The four views are final:
Python can neither construct nor subclass them. See the
[per-virtual execution inventory](callback-paths.md).

The host-view API:

- `rackpy.raw.context_get().scene` is a `HostScene`. It has `rack`,
  `rack_scroll`, `menu_bar` and `browser`, `get_mouse_pos()`, and Rack's
  deprecated `mouse_pos` field.
- `HostRackScrollWidget` exposes `zoom_widget` and `rack_widget` host views,
  and has `get_zoom()` and `set_zoom(zoom, pivot=)`
  (Rack clamps the zoom to 1/4 to 4), `get_grid_offset()`,
  `set_grid_offset()`, `reset()`, `zoom_to_modules()` and
  `zoom_to_bound(rect)`. The bound must be finite with a positive size,
  because Rack divides by it.
- `HostRackWidget` covers:
  - modules: `get_module(id)`, `get_modules()`, and `request_module_pos`,
    `set_module_pos_nearest`, `set_module_pos_force` and
    `set_module_pos_squeeze` for a module widget in the rack;
  - Rack's selection and its actions: `select`, `select_all`,
    `deselect_all`, `has_selection`, `is_selected`, `get_selected`,
    `is_selection_bypassed`, `selection_to_json`, load and save, clipboard,
    reset, randomize, disconnect, clone, bypass and delete. Use
    `append_selection_context_menu(menu)` to add Rack's selection menu items.
    `update_selection_from_rect()` updates the selection from Rack's current
    selection rectangle; `request_selection_pos(delta)` moves the selection
    if the new positions do not overlap other modules, while
    `set_selection_pos_nearest(delta)` searches nearby grid positions;
  - patch JSON: `merge_json` and `from_json`. `from_json` refuses modules or
    cables that already have widgets, because Rack would give them a second
    owner;
  - paste: `paste_json_action`, `paste_module_json_action` and
    `paste_clipboard_action`;
  - `clear()` and cable clearing: `clear_cables()` removes all cables
    without an undo step; `clear_cables_action()` records an undo step, and
    `clear_cables_on_port(port)` removes those on one port;
  - Rack's child containers: `get_module_container()`,
    `get_cable_container()` and `get_plug_container()` return host views;
  - cable lookups (`get_cables`, `get_complete_cables`,
    `get_incomplete_cables`, deprecated `get_incomplete_cable`,
    `get_cable(id)`, `get_cable(output, input)`, `get_cables_on_port`,
    `get_complete_cables_on_port`, `get_top_cable`, `get_top_plug`) and the
    next cable colour;
  - `get_touched_param()`, `set_touched_param()` and `touched_param`, the
    param Core's MIDI-Map learns. That param needs a module.
- `add_module(module_widget)` and `add_module_at_mouse(module_widget)` put a
  Python module widget, with its model and module, into the rack, which then
  owns it. `remove_module(module_widget)` takes it out again, disconnecting
  its cables, and Python owns it once more. A module widget Rack made is
  deleted with its `remove_action`.
- Rack 2.6.6's `has_modules()` is inverted: it returns True only for an
  empty rack, because it returns `children.empty()`.

Calls that take Rack's engine lock release the GIL, and calls that change
the widget tree are refused inside a widget callback. The dialog methods
wait for the user.

Every host view can also run Rack's own behaviour: `step()`, `draw(args)`
and `draw_layer(args, layer)` (inside a drawing callback), and its event
handlers (`on_hover`, `on_button`, ...) with RackPy's event records. Only
`on_add`, `on_remove` and the graphics context events are missing, because
they announce changes only Rack makes.

(app-cables)=
## Rack's cables

The cables users plug are Rack's `CableWidget`s, which reach Python as
`HostCableWidget` views:

- `input_port`, `output_port` and the hovered ports, `input_plug` and
  `output_plug` (`HostPlugWidget`s);
- `is_complete()`, `get_plug(type)`, `get_port(type)` and
  `get_hovered_port(type)`, `get_input_pos()` and `get_output_pos()`;
- the `color`, `merge_json()` and `from_json()`;
- `update_cable()`, which replaces the engine cable.

A `HostPlugWidget` has `get_cable()` and `get_type()`. Python gets plugs
only from a cable widget. Rack's `PlugWidget()` leaves a plug's cable and
type unset, and only `CableWidget`'s constructor sets them. So no plugin can
make a usable plug of its own, and `PlugWidget.step` maps to calling Rack's
step on Rack's plugs.

(app-cable-widgets)=
### Cable widgets Python makes

`CableWidget()` is Rack's cable widget, made by Python and subclassable,
with the same fields and methods (**RP-28E**). It follows Rack's own cable
dragging:

```{rackpy-example} cable_widget.py
```

- **Ports.** `input_port` and `output_port` take port widgets of the right
  direction that Rack can cable: Rack's own, or a Python module widget's
  factory ports. While the widget is detached, RackPy drops a port whose
  module widget dies.
- **Where it lives.** A cable widget lives only in Rack's cable container
  (`RackWidget.add_cable`). Its Add and Remove handlers move its plugs in
  and out of Rack's plug container, so only Rack's dispatch runs them; a
  subclass's `super().on_add(e)` reaches Rack's code then. Rack's module,
  cable and plug containers each hold only their own kind of widget, as
  Rack assumes.
- **Engine cables.** `update_cable()` and `set_cable(cable)` change Rack's
  engine, so the widget must be in the rack first, as in Rack's dragging.
  Then no engine cable outlives its modules. `set_cable` adopts a `Cable`
  Python made: RackPy adds it to the engine, as Rack requires, and the
  widget takes its ports from it. `set_cable(None)` removes and deletes the
  widget's engine cable. `cable` and `get_cable()` return the engine cable,
  found by ID.
- **Taking cables back.** `release_cable()` gives up the engine cable.
  RackPy also takes it out of Rack's engine, because a cable without a
  widget between module widgets would crash Rack when a module goes, and
  Python owns it.
- **Leaving the rack.** `RackWidget.remove_cable(widget)` gives the widget
  back to Python, and RackPy removes its engine cable, as Rack's deletion of
  the widget right after would. Rack's own cable widgets leave with Rack's
  actions: the history's `CableRemove`, `delete_top_cable_action` or
  `disconnect_action`.

The plugs are Rack's, made by the cable widget and deleted with it.
`PlugWidget()` alone leaves Rack's plug without a cable, so Python reaches
plugs only through a cable widget. Rack's history (`CableAdd.set_cable`)
and undo work with Python's cable widgets as with Rack's.

(app-io-displays)=
## Audio and MIDI displays, buttons and menus

Rack's audio and MIDI widgets show a port and open its menus:

- `AudioDriverChoice`, `AudioDeviceChoice`, `AudioSampleRateChoice`,
  `AudioBlockSizeChoice` and `AudioDeviceMenuChoice`.
- `MidiDriverChoice`, `MidiDeviceChoice` and `MidiChannelChoice`.
- `AudioDisplay` and `MidiDisplay`, whose `set_audio_port` and
  `set_midi_port` rebuild their choices (Rack's widgets). Their read-only
  `driver_choice`, `driver_separator`, `device_choice` and `device_separator`
  are host views or `None`. Audio also exposes `sample_rate_choice`,
  `sample_rate_separator` and `buffer_size_choice`; MIDI has `channel_choice`.
- `AudioButton` and `MidiButton`, with the component library's
  `AudioButton_ADAT`, `AudioButton_USB_B` and `MidiButton_MIDI_DIN`, which
  use Rack's artwork.
- `append_audio_menu(menu, port)` and `append_midi_menu(menu, port)`.

Each choice's `on_action()` opens Rack's corresponding menu; the buttons
open the combined menus. In Rack 2.6.6 the combined MIDI menu includes driver
and device choices, but not the channel choice; `MidiChannelChoice` opens its
own channel menu.

They take ports Python made ([audio](audio.md), [MIDI](midi.md)). Rack's
widgets read their port every frame and their menus capture it, so RackPy
keeps the port alive while any widget or menu that points at it exists, and
lets it go once Rack deletes them.

(app-led-displays)=
## LED displays

`LedDisplay`, `LedDisplaySeparator`, `LedDisplayChoice` (`text`,
`font_path`, `text_offset`, `color`, `bg_color`) and `LedDisplayTextField`
(a `TextField` with the same appearance fields) are Rack's display widgets,
which a Python subclass can extend. `font_path` names a font Rack loads each
frame; with a missing font, no text is drawn.
