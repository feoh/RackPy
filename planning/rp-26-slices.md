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

**Next on RP-26:** menu helper templates from `helpers.hpp` (`createMenu`,
`createMenuItem`, `createBoolMenuItem`, `createIndexSubmenuItem`, ...) with
callback lifetimes that survive module deletion. Then `EventState` and
Rack's input dispatch, with the `Context` accessor (`contextGet`, `APP`,
`Context::event`); the `RACK_MOD_*` and `RACK_HELD` macros; the deprecated
`rack::event::*` aliases; and `Model::appendContextMenu` (from RP-16).
