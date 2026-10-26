# RP-26 bounded implementation slices

These child contracts supplement, not replace, [RP-26 in TASKS.md](TASKS.md).
Live Witan status is authoritative. EventState (32 records) joined RP-26 on
2026-10-23 (RP-25's closure).

**RP-26A — rack::ui widgets as subclassable Python classes**
(`tk-rackpy-26a-rack-ui-widgets-as-subclassable-pytho-893c63`). All 177
records in `include/ui/` are implemented as `rackpy.raw.ui`, on RP-25's
managed Widget machinery: Rack's hierarchy, one owner, contained overrides,
main thread.

- **Virtuals.** Class-specific virtuals have their own trampolines:
  `MenuItem::createChildMenu` and `TextField::getTextPosition`. A
  `FactoryScope` lets `create_child_menu` build a new detached menu even
  though Rack is dispatching an event, and the menu is handed to Rack.
- **Pointer fields.** Rack does not own the `quantity` fields of `Button`,
  `Slider` and `ProgressBar`, so the widget keeps the Python Quantity alive
  until it is deleted. `TextField.prev_field`/`next_field` accept only
  RackPy-managed widgets and are cleared when the target is deleted.
- **Ownership methods.** `Menu.set_child_menu` transfers through RackPy's
  ownership. `HostWidget.add_child` hands content to a Rack-owned widget
  such as a `ScrollWidget`'s container.
- **Tests.** The actual-Rack live-UI tests also run Rack's own drawing of
  each widget through a framebuffer.

**RP-26B — menu helpers, Context accessor and EventState**
(`tk-rackpy-26b-menu-helpers-context-accessor-and-eve-66c09a`). 89 records:

- **Menu helpers.** The `helpers.hpp` menu templates are factories in
  `rackpy.raw`. `create_menu` is native (a menu at the mouse in a scene
  overlay); the rest are Python subclasses around Python callables, with
  `item_type`/`label_type` for the template parameter and `Ref` for the
  pointer variants. A callback for a deleted module fails contained.
- **Context and input.** `context_get()`, `APP()` and `Context.event` are
  bound; the Context record stays planned while its other fields wait for
  their families. `EventState` has checked fields (writes take only live
  widgets; Rack's finalizeWidget clears them), the set*/finalize methods and
  every `handle*` dispatcher, which run Rack's input through Python widgets.
- **Constants and aliases.** The `RACK_MOD_*`/`RACK_HELD` macros, and
  `rack::event` aliases that warn on use. The introspection snapshot resolves
  lazily provided names.
- **From RP-16.** `Model.append_context_menu`.

All of RP-26's records are implemented. Tests run in actual Rack's live UI:
keyboard, text (including an emoji code point), scroll, mouse button,
hover, select and leave, raw field writes and their finalization, every
menu helper including submenus and index menus, and a menu outliving its
module.
