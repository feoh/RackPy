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
