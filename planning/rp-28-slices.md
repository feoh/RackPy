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
| 28D | Scene, RackWidget, RackScrollWidget, Rack's cable and plug widgets, and the LED displays |
| 28E | Python cable widgets and rack cable edits (with RP-24's engine cables), and the audio and MIDI displays and buttons (with RP-29) |
| 28F | The UI virtuals of `PlugWidget`, `RackScrollWidget`, `RackWidget` and `Scene`, which no plugin can override: mapped as calls on Rack's own instances (decision) |

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

**RP-28D — scene, rack, scroll, Rack's cables and LED displays**
(`tk-rackpy-28d-scene-rack-scroll-rack-s-cables-and-l-3d7ead`). 152 records.
Validation: [rp-28d-validation.md](rp-28d-validation.md).

- **Host views.** Rack makes and owns its Scene, RackScrollWidget and
  RackWidget, and the cable and plug widgets of the cables users plug, so
  they are typed host views: `Context.scene` gives a `HostScene`, and so on.
- **Host dispatch.** Every host view can run Rack's step, drawing (inside a
  drawing callback) and event handlers. Decision: Add, Remove and the
  graphics context events are not sent from Python, because they announce
  structural changes only Rack makes.
- **RackWidget.** Covered: module lookup and placement, selection and its
  actions, patch JSON, paste, clipboard, cable queries and clearing, cable
  colours and the touched param. Decisions:
  - `from_json` refuses modules and cables that already have widgets.
  - `add_module` and `remove_module` hand a Python module widget to the
    rack and back.
  - The touched param must have a module.
  - Engine-locking calls release the GIL.
- **Rack 2.6.6 quirk.** `RackWidget::hasModules` returns
  `children.empty()`: True only for an empty rack. The binding returns
  Rack's value and documents it.
- **LED displays.** LedDisplay, LedDisplaySeparator, LedDisplayChoice and
  LedDisplayTextField are subclassable.
- **Moved to RP-28E** (`tk-rackpy-28e-python-cable-widgets-rack-cable-edits-7eedc5`,
  blocked by RP-24 and RP-29): Python-created cable and plug widgets and
  their `engine::Cable`, the Add/Remove handlers, `addCable` and
  `removeCable`, the audio and MIDI displays and buttons. RP-24 also takes
  `getModuleDragAction` (a history ComplexAction).

**RP-28E — Python cable widgets, rack cable edits, and the audio/MIDI
displays** (`tk-rackpy-28e-python-cable-widgets-rack-cable-edits-7eedc5`).
75 records, and `record:rack::Context` is complete. Validation:
[rp-28e-validation.md](rp-28e-validation.md).

- **Cable widgets.** `CableWidget` is a subclassable managed widget
  (`CableWidgetT`). It keeps RP-24B's invariant that no engine cable
  without a widget touches a module with a widget:
  - `update_cable`/`set_cable` act only while it is in Rack's rack.
  - `set_cable` adds a Python `Cable` to the engine and adopts it at once
    (`engineCableForWidget`, undone if Rack refuses).
  - `release_cable` and `RackWidget.remove_cable` also take the engine
    cable out.
  - Port fields take cable-safe ports of the right direction, and are
    weakly checked while detached.
  - Rack's dispatch alone runs `on_add`/`on_remove` (they move the plugs);
    a subclass's `super()` works inside it.
- **Containers.** `attachChild` now refuses a widget of the wrong kind in
  Rack's module, cable or plug containers, and a cable widget anywhere but
  the cable container. Rack asserts on such children, so this closes a
  latent RP-28D hazard (`get_*_container().add_child`).
- **Plugs.** Rack's `PlugWidget()` leaves its pimpl's cable and type
  uninitialized, so Python reaches plugs only as a cable widget's.
- **Audio/MIDI widgets.** Choices, displays, buttons, the component
  library's three buttons, and `append_audio_menu`/`append_midi_menu` take
  Python's RP-29 ports. Each port is pinned to the widget or menu with
  Rack's `WeakPtr` until Rack deletes it, because their `step()` and menu
  items read the raw pointer. Rack's headers leave those widgets' port and
  child pointers uninitialized, so Python-made ones start null
  (`PortWidgetInit`, `AudioDisplayInit`, `MidiDisplayInit`); without this,
  `AudioDriverChoice::step` crashed.
- **Fixed in the widget runtime.** `Widget<Base>::reclaim` handed a
  disowned widget back to Python through pybind11's `unique_ptr` cast,
  which looks the object up at the most-derived address. That misses an
  alias whose `Base` is not its first base, and it made a second owning
  peer, whose loss deleted the widget: a cable widget was deleted after
  `remove_cable` or `remove_from_parent`. `reclaim` now re-owns the
  registered instance as pybind11's own reclaim does (pinned pybind11
  3.1.0).

**RP-28F — host-only app widgets mapped as calls on Rack's instances**
(`tk-rackpy-28f-python-subclass-trampolines-for-plugw-154b61`). 21 records.
Validation: [rp-28f-validation.md](rp-28f-validation.md).

The RP-22D callback audit moved these 21 `ui` virtuals to `planned`: their
only path was RP-28D's borrowed host views, which call Rack's
implementations but take no Python override. The task was to give them
Python subclass trampolines, as RP-28E did for `CableWidget`.

- **Decision (supervisor, 2026-10-26; approved by the owner the same day).** Public API
  cannot override a virtual of a class whose usable instances only Rack can
  make. The call on Rack's own instances is then the mapping, and the record
  cites the header (and, if needed, Rack's source) lines. Recorded as a
  clarification of coverage policy 11 ([coverage](../docs/coverage.md)).
  - `Scene`, `RackScrollWidget` and `RackWidget` have PRIVATE constructors
    and destructors (`include/app/Scene.hpp:27-28`,
    `include/app/RackScrollWidget.hpp:19-20`,
    `include/app/RackWidget.hpp:28-29`), so no plugin can subclass them.
  - `PlugWidget`'s constructor and destructor are public
    (`include/app/CableWidget.hpp:20-21`). But Rack's `PlugWidget()` leaves
    its pimpl's cable and type indeterminate, and its `step()` dereferences
    the cable (Rack 2.6.6's `src/app/CableWidget.cpp:36-49, 52-103`). Only
    `CableWidget`'s constructor sets them (`src/app/CableWidget.cpp:140-151`),
    and it makes its own plugs. So a plugin's subclass can never be a cable's
    plug, and Rack's `step` on one would dereference that indeterminate
    pointer. This agrees with RP-28E's decision that Python gets plugs only
    from a cable widget.
- **Records.** The 21 records are `implemented` again. Each note starts
  with the decision and its evidence, and keeps the RP-28D text. The
  generated [callback paths](../docs/reference/callback-paths.md) say "Call
  on Rack's own instance" for them (`bin/generate_bindings.py`,
  `HOST_ONLY_APP_WIDGETS`).
- **Final views.** `HostScene`, `HostRackScrollWidget`, `HostRackWidget` and
  `HostPlugWidget` are now final, so a Python subclass that looks like an
  override is refused when its class is defined. They already had no
  constructor.
- **Evidence in Rack's live UI.** The new in-Rack test checks that
  `rackpy.raw.app` has no class to make these with and that the four views
  refuse construction and subclassing. It also checks that the views run
  Rack's implementations on Rack's own instances:
  - `Scene`: stepping, a path drop, hover and drag-hover positions, Ctrl+0
    and the held-arrow scroll.
  - `RackScrollWidget`: paging, scrolling, the fourth mouse button's zoom,
    hover passed on to the rack, and the step's offset clamp.
  - `RackWidget`: the drag-selection rectangle, a left click and key
    propagation.
  - Drawing all three into a framebuffer, and stepping a cable widget's
    `HostPlugWidget`.
