# RP-28 bounded implementation slices

RP-28 (`tk-rackpy-28-bind-app-widgets-component-library-and-622ff6`) binds
`rack::app`, the component library and `helpers.hpp`'s widget factories. It
is split into bounded children, each closed with a commit on `main`.
Decisions follow the owner's standing instruction to take the recommended
option (2026-10-25).

| Slice | Scope |
| --- | --- |
| 28A | Controls, ports, lights, SVG widgets, panels and rails; typed host views; widget factories |
| 28B | `ModuleWidget`; Python module-widget factories (`createModel`'s widget, `Model::createModule`/`createModuleWidget`); ports those widgets create, which cable; the real module-browser smoke gate (RP-25's) |
| 28C | The component library, including `createLightParam` and its light buttons; themed widgets switching with `preferDarkPanels` (with RP-30) |
| 28D | Scene, RackWidget, RackScrollWidget, CableWidget, and the LED, audio and MIDI displays |

**RP-28A — app controls, typed host views and widget factory helpers**
(`tk-rackpy-28a-app-controls-typed-host-views-and-wid-45d11d`). 219 records.
Validation: [rp-28a-validation.md](rp-28a-validation.md).

- **Controls.** Every class in the app control headers is a subclassable
  Python class. Python overrides cover `initParamQuantity`,
  `appendContextMenu` (which may fill the menu Rack is building) and
  LightWidget's `drawBackground`, `drawLight` and `drawHalo`.
- **Module fields.** A control takes only a RackPy module in Rack's engine.
  Its IDs are checked against the module's fixed counts, because Rack indexes
  them unchecked. The pointer is cleared through `Module::trackBackreference`
  when Rack deletes the module.
- **Raw pointers.** Rack keeps raw pointers to Python controls in two places:
  `touchedParam`, and the context-menu items that point at their control.
  RackPy releases or closes them when a control dies.
- **Cables.** Decision: Python ports take no part in cables until RP-28B's
  module widgets create them. Rack's undo (`CableAdd::redo`, then
  `CableWidget::setCable`) and patch loading find ports through their module
  widget and throw for one it did not create.
- **SVGs.** Rack's `svgDraw` dereferences the parsed image unchecked, so
  widgets Rack draws take only loaded SVGs. `Svg.load_file` keeps the
  previous image on failure, unlike Rack's.
- **Host views.** Rack's own child widgets (`fb`, `sw`, `tw`, `shadow`) are
  typed host views of their public fields.
- **Factories.** `create_widget`, `create_panel` (both overloads),
  `create_param`, `create_input`, `create_output` and `create_light`, with
  their centred versions.
- **PRIVATE.** The macro has no run-time meaning, so family 0 now has a
  no-op adapter.

**RP-28B — ModuleWidget, Python module widget factories and the module-browser gate**
(`tk-rackpy-28b-modulewidget-python-module-widget-fac-ddcbde`). 65 records.
Validation: [rp-28b-validation.md](rp-28b-validation.md).

- **ModuleWidget.** A subclassable Python class with Rack's full API: panel,
  controls, scans, JSON, presets, templates, clipboard, actions, context menu
  and grid. Decision: `set_module` takes only the module Rack passes the
  widget's factory, once. Rack's `setModule` deletes a module it replaces,
  after `Engine::removeModule`, which assumes the module is in the engine and
  has no cables. Engine-locking calls release the GIL.
- **Widget factories.** `create_model(..., widget_factory=)` and
  `add_model(..., widget=)` follow Rack's `createModel<TModule,
  TModuleWidget>`: the module, or None for a preview; the module check;
  `setModel`; then Rack takes the widget. A factory that fails gives the
  module back untouched (RackPy clears the claimed widgets' pointers), and
  Rack gets the plain panel. Ports a factory creates take part in cables.
- **Model calls.** `Model.create_module()` and
  `Model.create_module_widget()` serve RackPy models, and Python owns the
  results. Other plugins' models wait for RP-24's foreign module handles, so
  those two records stay planned.
- **Shutdown moved (ADR 003 decision 13, amended).** RackPy's windowed
  shutdown now begins at scene deletion, as headless already did. Rack saves
  its autosave after deleting its window, and RackPy's teardown at window
  close had deleted the Python module widgets and the modules they own, so
  the patch lost them. Graphics still release at the window's
  ContextDestroy.
- **Fixed.** The `engine_adopt` test hook no longer overwrites a module's
  model.

`appendDisconnectActions` takes a `history::ComplexAction`, so it waits for
the history family (RP-24's history-facing operations).

**RP-28C — the component library** (`tk-rackpy-28c-the-component-library-f63202`).
256 records, plus `createLightParam` and `createLightParamCentered`.
Validation: [rp-28c-validation.md](rp-28c-validation.md). The task was
created after this slice's work had begun, and claimed when it was created.

- **Components.** Rack's concrete classes are bound natively, so their own
  constructors load Rack's installed artwork and none is redistributed. The
  knobs' `bg` and Rogan's `fg` are host views.
- **Templates.** Decision: each CRTP template is a Python *template class*
  holding the template's header code. `Template[args]` gives a cached
  specialization deriving from both the template class and its base, so
  `isinstance(x, MediumLight)` holds. Rack's aliases are the default
  specializations, and alias templates give the aliased specialization.
  Each template reads its own arguments (`_args_<Template>`), so a
  specialization deriving from another keeps the base's.
- **Other parts.** `SegmentDisplay.set_lights` is Rack's
  `setLights<TLightBase>`. `create_light_param` and its centred version use
  `get_light()`. The `SCHEME_*` colours are bound.
- **Build time.** The bindings are split over five translation units. One
  file took 181 s to compile; a fresh build is back to 96 s on 32 cores.
- **Moved.** `AudioButton_*` and `MidiButton_*` go to RP-28D, with the
  displays they derive from.
