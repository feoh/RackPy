# Trampoline reentry audit and validation

Task `tk-rackpy-trampolines-suppress-same-object-re-entra-0abce9`, branch
`trampoline-reentry`, based on `origin/main` `7d2c543`. Linux x86_64,
GCC 16.2.1, bundled CPython 3.14, pybind11 3.1.0, pinned Rack Free/SDK 2.6.6.
All artifacts below belong to this worktree's ignored `build/validation/`.
No push, publication, audio/MIDI hardware access, or other worktree edits.

## Contract and decisions

The old resolver's same-object/same-name Python-frame check incorrectly
sent native reentrant virtual calls to the base. `pythonOverride` now uses
the pinned pybind11 registered-instance lookup and skips `cpp_function`
base bindings, without inspecting Python frames or caching negative results. Lookup itself
is bounded as well, since descriptors / `__getattribute__` can reenter native
code. Defaulted `py::getattr` retains pinned pybind11's existing policy of
clearing lookup errors and using the native fallback; this differs from an
error raised while invoking an override.
Its returned non-copyable/non-movable `PythonOverride` bounds the entire
resolved-callable lifetime (including argument/result conversion and
bound-method destruction), so
all call sites (including custom event/draw/JSON/queued adapters) share the
same policy without losing their family-specific scopes or error boundaries.

Supervisor-approved decisions:

- Native base bindings reached on a **RackPy trampoline** use the qualified
  implementation of the binding's C++ class. This includes `super()` and
  explicit `Base.method(instance)` even outside the override; the latter
  previously could redispatch. Calls on **native C++ subclasses** remain
  virtual. Quantity aliases are identified by their existing
  `LaboratoryQuantity` polymorphic base (present even without lab enablement),
  metadata/history/device aliases by their concrete alias or existing marker.
  MIDI `Port` is abstract: Python ports use the qualified concrete
  `Input`/`Output` implementation, not a pure-virtual `Port` body.
- A shared thread-local RAII counter allows 64 simultaneously live
  native-to-Python override invocations. This is deliberately below Python's
  usual 1,000-frame limit: Python cannot see all Rack/C++ frames between
  callbacks. On overflow, `RecursionError` follows the family's existing
  propagation or containment/fallback policy; every unwind restores depth.
  An exhaustion latch refuses all further entries after the first overflow
  until the whole chain unwinds to zero. This prevents pybind11 numeric-caster
  retries (which clear errors and call `__float__` again) from expanding into
  a fresh recursion tree. Conversion errors retain pybind11's existing outer
  RuntimeError; an override body without an intervening caster exposes
  RecursionError. Finite reentrant conversion, bounded infinite conversion
  (64 callbacks / at most 128 conversions), and successful reuse are tested.
  It is a safety bound, not a performance or arbitrary-native-stack guarantee.
  Laboratory Module's independently implemented callbacks retain their
  existing separate 200-level bound. Bench/engine lock reentry is still refused.
- Test-hooks-only `call_virtual` explicitly dispatches representative native
  virtuals (no reflective lookup), permitting same-object tests without
  cyclic owning trees or recursively opened menus. The native Quantity
  fixture is deliberately not a RackPy trampoline. Neither hook exists in
  production. Natural Rack callers are tested separately too.

`callPython` itself is unchanged: its interpreter/thread refusal, GIL,
exception logging and deferred-release handling still surround Rack calls.
The shared helpers still propagate to GIL-held Python callers and use their
old fallback for failed/refused Rack calls. Widget/event/draw/history and
JSON adapters retain their existing containment/ErrorCapture policies.
Generator callback messages remain owned copies; other argument conversions
and callback-scoped event/draw records are unchanged.

## Complete super-path audit

The table enumerates all trampolined virtuals found by
`grep -rn 'overrideOr\|get_override\|PYBIND11_OVERRIDE' native`, expanded
through the widget event macro and shared quantity templates, plus the
laboratory Module's separately stored callbacks. Inherited Widget virtuals
apply to **all** bound widget/UI/app/component-library variants. Qualified
paths below were already nonvirtual unless marked **fixed**. Host views
continue to call native objects virtually; they are not Python subclass bases.

| Family / virtual(s), Python spelling | Python-visible base/super path after audit |
| --- | --- |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_value` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getValue`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `set_value` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::setValue`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_min_value` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getMinValue`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_max_value` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getMaxValue`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_default_value` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getDefaultValue`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_display_value` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getDisplayValue`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `set_display_value` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::setDisplayValue`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_display_precision` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getDisplayPrecision`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_display_value_string` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getDisplayValueString`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `set_display_value_string` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::setDisplayValueString`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_label` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getLabel`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_unit` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getUnit`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `get_string` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::getString`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `reset` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::reset`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| Quantity / ValueQuantity / ParamQuantity / SwitchQuantity: `randomize` | **Fixed** `values/quantity.cpp`, `engine/standalone_quantity.cpp`: alias → qualified declaration (`Quantity::randomize`, or the more-derived bound declaration where overridden); native subclass → virtual. Access/context/write checks retained. Value/range Python overrides still lab-only. |
| ParamQuantity / SwitchQuantity: `get_description` | **Fixed** qualified `ParamQuantity::getDescription` for aliases; virtual for native subclasses. JSON keeps ErrorCapture; dict-only override output unchanged. |
| ParamQuantity / SwitchQuantity: `to_json` | **Fixed** qualified `ParamQuantity::toJson` for aliases; virtual for native subclasses. JSON keeps ErrorCapture; dict-only override output unchanged. |
| ParamQuantity / SwitchQuantity: `from_json` | **Fixed** qualified `ParamQuantity::fromJson` for aliases; virtual for native subclasses. JSON keeps ErrorCapture; dict-only override output unchanged. |
| SwitchQuantity: `get_display_value_string`, `set_display_value_string` overrides | **Fixed** qualified `SwitchQuantity` implementations, not ParamQuantity; label-index representability checks retained. |
| PortInfo, LightInfo: `get_name`, `get_description` | **Fixed** `components.cpp::infoMethod`: qualified bound `Info` implementation for aliases; native subclasses still virtual. `get_full_name` remains a native caller of virtual `getName`. |
| All history action types: `undo`, `redo` | **Fixed** `history.cpp::pythonRun` → `rackRun<T>` for aliases → `a.T::undo/redo`. Native subclasses remain virtual. `_rack_undo/_rack_redo` remain explicitly nonvirtual; InverseAction keeps that pattern. Types: Action, ComplexAction, ModuleAction, ModuleAdd, ModuleRemove, ModuleMove, ModuleBypass, ModuleChange, ParamChange, CableAdd, CableRemove, CableColorChange. |
| Widget and all variants: `step` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::step`. |
| Widget and all variants: `get_children_bounding_box` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::getChildrenBoundingBox`. |
| Widget and all variants: `get_visible_children_bounding_box` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::getVisibleChildrenBoundingBox`. |
| Widget and all variants: `get_relative_offset` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::getRelativeOffset`. |
| Widget and all variants: `get_relative_zoom` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::getRelativeZoom`. |
| Widget and all variants: `get_viewport` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::getViewport`. |
| Widget and all variants: `draw` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::draw`. |
| Widget and all variants: `draw_layer` | `widget/behavior.hpp::bindBehavior` → `native<Base>(self).Base::drawLayer`. |
| Widget and all variants: `draw_vg` | `behavior.hpp` → `legacyDraw` → `Widget::draw(NVGcontext*)`, explicitly nonvirtual; deprecated behavior retained. |
| FramebufferWidget, OpenGlWidget: `draw_framebuffer` | `variants_framebuffer.cpp::framebufferCallback` → `w.Base::drawFramebuffer`, with framebuffer/access/drawing checks. Non-framebuffer aliases have an internal unused slot, not a Python virtual. |
| Widget event `on_hover` (`HoverEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onHover`. Scoped records and target capture unchanged. |
| Widget event `on_button` (`ButtonEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onButton`. Scoped records and target capture unchanged. |
| Widget event `on_double_click` (`DoubleClickEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDoubleClick`. Scoped records and target capture unchanged. |
| Widget event `on_hover_key` (`HoverKeyEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onHoverKey`. Scoped records and target capture unchanged. |
| Widget event `on_hover_text` (`HoverTextEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onHoverText`. Scoped records and target capture unchanged. |
| Widget event `on_hover_scroll` (`HoverScrollEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onHoverScroll`. Scoped records and target capture unchanged. |
| Widget event `on_enter` (`EnterEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onEnter`. Scoped records and target capture unchanged. |
| Widget event `on_leave` (`LeaveEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onLeave`. Scoped records and target capture unchanged. |
| Widget event `on_select` (`SelectEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onSelect`. Scoped records and target capture unchanged. |
| Widget event `on_deselect` (`DeselectEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDeselect`. Scoped records and target capture unchanged. |
| Widget event `on_select_key` (`SelectKeyEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onSelectKey`. Scoped records and target capture unchanged. |
| Widget event `on_select_text` (`SelectTextEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onSelectText`. Scoped records and target capture unchanged. |
| Widget event `on_drag_start` (`DragStartEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDragStart`. Scoped records and target capture unchanged. |
| Widget event `on_drag_end` (`DragEndEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDragEnd`. Scoped records and target capture unchanged. |
| Widget event `on_drag_move` (`DragMoveEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDragMove`. Scoped records and target capture unchanged. |
| Widget event `on_drag_hover` (`DragHoverEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDragHover`. Scoped records and target capture unchanged. |
| Widget event `on_drag_enter` (`DragEnterEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDragEnter`. Scoped records and target capture unchanged. |
| Widget event `on_drag_leave` (`DragLeaveEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDragLeave`. Scoped records and target capture unchanged. |
| Widget event `on_drag_drop` (`DragDropEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDragDrop`. Scoped records and target capture unchanged. |
| Widget event `on_path_drop` (`PathDropEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onPathDrop`. Scoped records and target capture unchanged. |
| Widget event `on_action` (`ActionEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onAction`. Scoped records and target capture unchanged. |
| Widget event `on_change` (`ChangeEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onChange`. Scoped records and target capture unchanged. |
| Widget event `on_dirty` (`DirtyEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onDirty`. Scoped records and target capture unchanged. |
| Widget event `on_reposition` (`RepositionEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onReposition`. Scoped records and target capture unchanged. |
| Widget event `on_resize` (`ResizeEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onResize`. Scoped records and target capture unchanged. |
| Widget event `on_add` (`AddEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onAdd`. Scoped records and target capture unchanged. |
| Widget event `on_remove` (`RemoveEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onRemove`. Scoped records and target capture unchanged. |
| Widget event `on_show` (`ShowEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onShow`. Scoped records and target capture unchanged. |
| Widget event `on_hide` (`HideEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onHide`. Scoped records and target capture unchanged. |
| Widget event `on_context_create` (`ContextCreateEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onContextCreate`. Scoped records and target capture unchanged. |
| Widget event `on_context_destroy` (`ContextDestroyEvent`) | `behavior.hpp::BIND_EVENT` → `w.Base::onContextDestroy`. Scoped records and target capture unchanged. |
| UI MenuItem / ColorDotMenuItem: `create_child_menu` | `ui.cpp`: qualified `MenuItem::createChildMenu` (ColorDotMenuItem inherits it); existing ownership transfer unchanged. |
| UI TextField / PasswordField; app LedDisplayTextField: `get_text_position` | `ui.cpp` / `app/rack.cpp`: qualified TextField / LedDisplayTextField base implementations. |
| app ParamWidget and controls: `init_param_quantity` | `app_internal.hpp::paramMethods`: `native<T>(self).T::initParamQuantity`. |
| app ParamWidget, PortWidget, ModuleWidget: `append_context_menu` | `paramMethods`, app port/module-widget binding lambdas: qualified bound `T::appendContextMenu`. Callback-scoped menu/trackMenu unchanged. |
| app PortWidget: `on_drag_start`, `on_drag_enter`, `on_drag_drop` specializations | `guardPortEvents`: qualified `T` event implementation, with existing cable-safety checks. |
| app CableWidget: `on_add`, `on_remove` specializations | Existing Widget event base paths; native CableWidgetT maintains cable registration around `trampolineEvent`. |
| app LightWidget / ModuleLightWidget: `draw_background`, `draw_light`, `draw_halo` | `app.cpp`: qualified bound base draw methods, checked scoped DrawArgs; generic `auto&` preserves guarded callable through `pyDraw`. |
| MIDI Input / InputQueue / Output: `get_device_ids` | **Fixed** `midi.cpp::portImplementation`: aliases → qualified concrete Input/Output; foreign/native ports → virtual. Context/subscription checks retained. |
| MIDI Input / InputQueue / Output: `get_default_device_id` | **Fixed** `midi.cpp::portImplementation`: aliases → qualified concrete Input/Output; foreign/native ports → virtual. Context/subscription checks retained. |
| MIDI Input / InputQueue / Output: `set_device_id` | **Fixed** `midi.cpp::portImplementation`: aliases → qualified concrete Input/Output; foreign/native ports → virtual. Context/subscription checks retained. |
| MIDI Input / InputQueue / Output: `get_device_name` | **Fixed** `midi.cpp::portImplementation`: aliases → qualified concrete Input/Output; foreign/native ports → virtual. Context/subscription checks retained. |
| MIDI Input / InputQueue / Output: `get_channels` | **Fixed** `midi.cpp::portImplementation`: aliases → qualified concrete Input/Output; foreign/native ports → virtual. Context/subscription checks retained. |
| MIDI Input: `on_message` (queued UI delivery) | Existing base binding is checked no-op; InputQueue base calls `InputQueue::onMessage` nonvirtually. Native driver callback still queues, never runs Python; only queued delivery resolves the override. Queue timing is unchanged, not made synchronous. |
| Audio Port: `process_buffer` | Existing checked no-op base binding (Rack Port default); Python-driven device dispatch / queued stream delivery now uses guarded resolver. Native production processing remains counting/no-op, never Python. |
| Audio Port: `process_input` | Existing checked no-op base binding (Rack Port default); Python-driven device dispatch / queued stream delivery now uses guarded resolver. Native production processing remains counting/no-op, never Python. |
| Audio Port: `process_output` | Existing checked no-op base binding (Rack Port default); Python-driven device dispatch / queued stream delivery now uses guarded resolver. Native production processing remains counting/no-op, never Python. |
| Audio Port: `on_start_stream` | Existing checked no-op base binding (Rack Port default); Python-driven device dispatch / queued stream delivery now uses guarded resolver. Native production processing remains counting/no-op, never Python. |
| Audio Port: `on_stop_stream` | Existing checked no-op base binding (Rack Port default); Python-driven device dispatch / queued stream delivery now uses guarded resolver. Native production processing remains counting/no-op, never Python. |
| Laboratory MidiOutputDevice: `send_message` | **Fixed** qualified `OutputDevice::sendMessage` for alias; native subclass stays virtual. Main-thread checks and synchronous Output caller unchanged. |
| Laboratory MidiGenerator<N>: `on_message` | `laboratory.MidiGenerator` → existing `_on_message` → `RecordingGenerator<N>::onMessage` nonvirtually; retains owned Message copy and native enablement mark. All N share the fix. |
| Test-only ProbeChild: `on_removed` | **Fixed** qualified `ProbeChild::onRemoved` for alias; native subclasses virtual. `callPython` still contains errors. |
| Laboratory Module: `process` | Existing `laboratory.py::ModuleClass` → `base_process` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `process_bypass` | Existing `laboratory.py::ModuleClass` → `base_process_bypass` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `step` | Existing `laboratory.py::ModuleClass` → `base_step` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_add` | Existing `laboratory.py::ModuleClass` → `base_on_add` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_remove` | Existing `laboratory.py::ModuleClass` → `base_on_remove` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_bypass` | Existing `laboratory.py::ModuleClass` → `base_on_bypass` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_un_bypass` | Existing `laboratory.py::ModuleClass` → `base_on_un_bypass` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_port_change` | Existing `laboratory.py::ModuleClass` → `base_on_port_change` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_reset` | Existing `laboratory.py::ModuleClass` → `base_on_reset` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_randomize` | Existing `laboratory.py::ModuleClass` → `base_on_randomize` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_save` | Existing `laboratory.py::ModuleClass` → `base_on_save` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_sample_rate_change` | Existing `laboratory.py::ModuleClass` → `base_on_sample_rate_change` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_set_master` | Existing `laboratory.py::ModuleClass` → `base_on_set_master` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_unset_master` | Existing `laboratory.py::ModuleClass` → `base_on_unset_master` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `on_expander_change` | Existing `laboratory.py::ModuleClass` → `base_on_expander_change` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `module_to_json` | Existing `laboratory.py::ModuleClass` → `base_module_to_json` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `module_from_json` | Existing `laboratory.py::ModuleClass` → `base_module_from_json` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `params_to_json` | Existing `laboratory.py::ModuleClass` → `base_params_to_json` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `params_from_json` | Existing `laboratory.py::ModuleClass` → `base_params_from_json` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `data_to_json` | Existing `laboratory.py::ModuleClass` → `base_data_to_json` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |
| Laboratory Module: `data_from_json` | Existing `laboratory.py::ModuleClass` → `base_data_from_json` → qualified Rack/RackPy Module implementation in `native/laboratory/module.cpp`, with `forPython`/ErrorCapture. Stored PyRef callbacks never used get_override; separate 200-level bound retained. |

Ordinary production Module data/lifecycle handlers are registered callables,
not pybind11 override lookup or super bindings, and remain unchanged. The
production native-only audio/MIDI Driver/Device implementations are not
Python trampolines. No `py::get_override`/`PYBIND11_OVERRIDE` invocation
remains in `native/` after the conversion.

## Existing tests updated for explicit-base semantics

No assertion of native callback behavior was simply discarded. These tests
previously used a bound base descriptor as a virtual-dispatch test driver:

| Changed test | Old expectation | New expectation and equivalent native evidence |
| --- | --- | --- |
| `test_standalone_quantities.py::test_ui_virtuals_base_calls_and_errors` (ParamQuantity and SwitchQuantity) | All explicit Quantity/ParamQuantity/SwitchQuantity descriptors invoked that same Python override (11 UI virtuals), or propagated its ValueError. | Each descriptor now equals its corresponding native base's result and does not call that override; normal Python lookup still calls/raises. `tests/trampolines/in_rack/test_reentry.py` drives **all 11** through actual native virtual calls for both ParamQuantity and SwitchQuantity error propagation, plus reentry/super/depth cases. `get_string()` naturally reaches Labeled/Formatted overrides; explicit Quantity bases give Quantity's own answers, and ParamQuantity's overflow guard is asserted separately. |
| `test_attached_quantities.py::test_attached_ui_override_reentry_and_base_errors` | Explicit ParamQuantity description / Quantity label and display-setter descriptors forced virtual callbacks, including close/detach inside one. | Explicit ParamQuantity description returns its base text; the failing override is separately asserted, with native error coverage in the all-UI-virtual hook cases. Live Rack's `get_string()` reaches the label override and closes/detaches the owner; startup has no live value/display context, so it calls the Python label directly and still verifies teardown. Quantity's native `set_display_value_string()` parser calls the virtual display setter and exercises close/detach with a nonvirtual super. |
| `docs/snippets/standalone_quantities.py` | Explicit Quantity label was advertised as a native virtual driver yielding the caption. | Explicit Quantity base is empty; native `get_string()` must produce the full `Pitch (preview): 0 V`, which proves the caption override and native formatting. |
| `test_json.py::test_quantity_json_overrides` | `ParamQuantity.to_json(tagged)` included the Python-only tag. | Explicit base JSON contains only `value`; normal Python lookup includes the tag. Existing `Module.params_to_json()` still proves native dispatch, including a failing override; new JSON native-reentry/depth tests also use ErrorCapture. |
| `test_midi_generator.py::test_reentrant_same_generator_dispatches_as_cpp` (renamed pinned 22E test) | The nested controller message bypassed Python and went straight to the recorder. | Both note and controller reach Python; explicit super records each exactly once, in completion order. The new unbounded-generator test asserts RecursionError at 64, then successful reuse. |

The production native-thread refusal tests and existing native differential
fixtures are retained unchanged. No new standalone CTest was added: this
bug depends on Python wrapper identity/current frames and needs the real
embedded runtime. The test-only native virtual drivers and native-subclass
fixture exercise that C++ boundary in actual Rack, alongside the six
existing standalone native tests.

## Validation commands and outcomes

Every host invocation is serialized by `flock /tmp/rackpy-host-tests.lock`,
with combined stdout/stderr teed to a log under `build/validation/`. Shell
pipelines use `set -o pipefail` and record the pytest exit code separately.
Sphinx builds were serial (no `-j`, including docs-test subprocesses).

| Exact command | Result / raw counts | Evidence under `build/validation/` |
| --- | --- | --- |
| `uv sync --frozen` | Pass as the first operation; final frozen-environment recheck also passed. Test runner: CPython 3.14.2. | Initial output in session; recheck in `reentry-uv-sync-final.log`. |
| `CMAKE_BUILD_PARALLEL_LEVEL=3 bin/rackpy-dev build --werror --test-hooks` | Pass after final native changes: CTest **6/6**, 0 failed, 0.21s. | `reentry-build-final.log`, exit `0` in `reentry-build-final.exit`. |
| `uv run pytest -m unit` | Pass: **91 passed, 189 deselected**, 22.02s. | `reentry-unit-final.log`, `.exit` = `0`. |
| `prek run --all-files` | Pass: **14 hooks**, including Ruff, strict typing, planning, callback matrix, manifest/generation. | `reentry-prek-final4.log`, `.exit` = `0`. |
| `bin/rackpy-dev typecheck` | Pass: **254 source files + 9 individual CLI strict checks**, no issues. | `reentry-typecheck-final.log`, `.exit` = `0`. |
| `uv run bin/generate_bindings.py check` | Pass: generated files match manifest. Generation was `uv run bin/generate_bindings.py generate`. | `reentry-generate-check.log`. |
| `uv run bin/check_coverage.py check --without-built-package` | Pass: **absent 2, excluded_internal 111, implemented 2942, planned 2, unreviewed 1**. No denominator/status changes; not built-package introspection or a full-coverage claim. | `reentry-coverage.log`. |
| `uv run sphinx-build -W --keep-going -n -b html docs build/docs/html` | Pass: **0 warnings**, serial HTML build. | `reentry-sphinx-final3.log`, `.exit` = `0`. |
| `uv run pytest -m 'docs and not host'` | Pass: **15 passed, 265 deselected**, 158.70s. | `reentry-docs-final4.log`, `.exit` = `0`. |
| `CMAKE_BUILD_PARALLEL_LEVEL=3 flock /tmp/rackpy-host-tests.lock timeout 5400 uv run pytest -m 'host and not dist' -ra --basetemp=build/validation/full-host-final -o tmp_path_retention_policy=all` | Pass: **114 passed, 166 deselected**, 1168.97s, **0 failed, 0 outer skips**; fresh production and hooks builds. | `reentry-full-host.log`, `.exit` = `0`; `full-host-final/` retained reports. |

The actual host is the pinned Rack Free 2.6.6, with live-context tests in
Xvfb/Mesa and startup tests in the production plugin. The full harness builds
its own fresh production and test-hooks plugins; no fixture substitution or
fake host is used.

## Final embedded results and intentional watchdog

All **29** retained `in_rack_result.json` reports have `exit = 0`;
aggregate **867 passed, 15 skipped, 0 failed**. These are embedded pytest
counts, not additional outer tests; they are intentionally reported separately
from the outer **114 passed**. Exact JSON paths, counts and every skipped
node/reason are retained in
`build/validation/reentry-full-host-final-json-counts.json`.

| Report directory under `build/validation/full-host-final/` | Passed | Skipped | Failed |
| --- | ---: | ---: | ---: |
| `in-rack-hooks0` | 50 | 0 | 0 |
| `in-rack-production0` | 47 | 3 | 0 |
| `test_app_widgets_in_live_rack0` | 36 | 0 | 0 |
| `test_audio_in_live_rack0` | 5 | 0 | 0 |
| `test_dsp_suite_passes_inside_r0/user` | 46 | 0 | 0 |
| `test_engine_in_live_rack0` | 13 | 0 | 0 |
| `test_expanders_in_rack0` | 7 | 0 | 0 |
| `test_history_in_live_rack0` | 5 | 0 | 0 |
| `test_kernel_libraries_load_and0/user` | 12 | 0 | 0 |
| `test_kernels_in_rack0` | 20 | 0 | 0 |
| `test_laboratory_in_production_0` | 37 | 6 | 0 |
| `test_laboratory_with_live_rack0` | 45 | 1 | 0 |
| `test_midi_in_live_rack0` | 8 | 0 | 0 |
| `test_modules_in_production_hos0` | 115 | 1 | 0 |
| `test_modules_with_live_rack_co0` | 149 | 0 | 0 |
| `test_plugin_api_in_live_ui0` | 15 | 0 | 0 |
| `test_rendering_in_live_rack0` | 27 | 0 | 0 |
| `test_settings_handlers_in_live0` | 3 | 0 | 0 |
| `test_settings_mutex_and_weak_p0` | 6 | 0 | 0 |
| `test_simd_suite_passes_inside_0/user` | 12 | 0 | 0 |
| `test_suite_passes_from_the_ui_0` | 19 | 0 | 0 |
| `test_suite_passes_inside_rack_0` | 15 | 4 | 0 |
| `test_system_and_network_in_rac0` | 8 | 0 | 0 |
| `test_trampoline_reentry_in_rac0` | 82 | 0 | 0 |
| `test_ui_in_live_rack0` | 16 | 0 | 0 |
| `test_utilities_in_rack0` | 6 | 0 | 0 |
| `test_virtual_devices_in_live_r0` | 8 | 0 | 0 |
| `test_widget_live_ui0` | 53 | 0 | 0 |
| `test_widget_startup_refusal0` | 2 | 0 | 0 |

The 15 embedded skips are deliberate configuration counterparts, not missing
reentry coverage: 4 borrowed-component tests need the hooks build; 1 bypass
native-reference test is absent from production; 6 laboratory startup cases
need live context and/or native-thread hooks; 1 laboratory live test is
startup-only; 3 production values tests need hooks. Their alternate hooks/live
or startup executions pass. All **82** new central trampoline cases execute
without skips, including both result-conversion regressions.

`tests/integration/test_lifecycle_probe.py::test_unsafe_worker_deadlocks_and_is_caught_by_the_watchdog`
**passed**, as did all 7 lifecycle probe tests. That test intentionally calls
`run_rack(timeout=30)` for the unsafe-worker deadlock and asserts returncode
`None`: its **30-second process-group watchdog kill is expected**. This is
not an outer `timeout 5400` failure; the full command completed with exit `0`.
No unexpected host crashes or outer timeout occurred.

## Development failures and other runs (not hidden)

- Initial docs tests: **1 failed, 14 passed, 265 deselected**, 131.64s.
  The copied-site test fixture intentionally omits `planning/`, so the new
  repository-relative link failed there. The docs now give the explicit
  source path as text instead; the final full docs run above passes.
- Initial targeted command:
  `CMAKE_BUILD_PARALLEL_LEVEL=3 flock /tmp/rackpy-host-tests.lock timeout 2400 uv run pytest -m 'host and not dist' tests/trampolines tests/laboratory tests/io/test_virtual.py --basetemp=build/validation/targeted -o tmp_path_retention_policy=all -ra`
  completed with **2 failed, 2 passed in 1016.10s**, exit `1`, retained in
  `reentry-targeted-host.log` and `targeted/` JSON reports. Central trampoline
  tests were **39 passed / 1 failed**: the new descriptor-recursion test had
  incorrectly expected RecursionError instead of pinned pybind11's safe
  defaulted-lookup fallback. It now asserts the native result and exactly
  64 lookups. Production laboratory was **36 passed / 6 skipped / 1 failed**:
  the new JSON-reentry test used `params_to_json`, whose public binding needs
  live context even with no parameters; it now uses `data_to_json` to test
  the same native virtual reentry in both startup/live modes. Live laboratory
  was **45 passed / 1 skipped**, virtual audio/MIDI suite **8 passed / 0 skipped**.
  This development run preceded the final lifetime/exhaustion hardening and
  expanded UI-virtual tests; the final fresh full suite is authoritative.
- First full host run: **3 failed, 111 passed, 166 deselected**, 1166.30s,
  exit `1` (`reentry-full-host-initial.log` / `.exit`, `full-host/` reports).
  All **82 central trampoline cases passed**, including both float-conversion
  regressions, all 11 native quantity-error cases for both classes, and the
  exact 64-callback limits. Failures were the previously overlooked executable
  standalone-quantity docs snippet's old explicit-base expectation, plus the
  new label-format assertion in both startup/live engine suites: SwitchQuantity
  intentionally omits the colon when its display is empty. The snippet now
  asserts the empty explicit base and exact natural native formatted caption;
  the quantity test asserts the exact label segment (with or without display),
  not an incorrectly mandatory delimiter. First full engine reports were
  **114 passed / 1 skipped / 1 failed** (startup), **148 passed / 1 failed**
  (live). There were no implementation changes after this full run; the final
  fresh full run revalidates the corrected executable expectations.
- Extra `uv run pytest -m 'native and not host'`: **29 passed, 251 deselected**,
  262.47s (`reentry-native-pytest.log`, exit `0`). This development run preceded
  the final Python-dispatch guard hardening; final standalone native evidence
  is the **6/6 CTest** result after all native edits above.
No behavioral test is xfailed or weakened to hide a failing implementation.
The native UI/error coverage explicitly replaces old descriptor-as-virtual
assertions; the above table documents each changed expectation.

## Remaining gates

Only Linux x86_64 Rack Free 2.6.6 is exercised. macOS, Windows, Rack Pro,
multi-context hosting, real audio/MIDI hardware and actual driver-thread
scheduling are not tested. Distribution/toolchain builds, inventory
re-extraction, performance benchmarking, CI and release/publication were not
run. The refusal probes establish guard behavior, not real-time safety or
hardware performance. Independent supervisor/reviewer acceptance remains a
separate gate. No push or Witan task/workflow mutation was performed here.
