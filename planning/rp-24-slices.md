# RP-24 bounded implementation slices

RP-24 (`tk-rackpy-24-bind-engine-operations-cables-paramete-e3a3a5`) binds
`rack::engine::Engine`'s operations, cables, parameter handles and the edits
that depend on Rack's context (history). It is split into bounded children,
each closed with a commit on `main`. Decisions follow the owner's standing
instruction to take the recommended option.

| Slice | Scope |
| --- | --- |
| 24A | `Engine` (`Context.engine`): modules, the master module, timing, params, JSON; `EngineModule` handles for modules RackPy did not create |
| 24B | `Cable` and the engine's cable operations; `ParamHandle` and its engine operations; Rack's `_NoLock` variants, if a callback that holds the lock can be proven |
| 24C | `rack::history` (`Context.history`), `ModuleWidget::appendDisconnectActions`, `RackWidget::getModuleDragAction` |
| 24D | `Model::createModule`/`createModuleWidget` for other plugins' models: Python-owned modules RackPy did not create |

**RP-24A — the Engine and handles for modules RackPy did not create**
(`tk-rackpy-24a-the-engine-and-handles-for-modules-ra-2ac8b1`). 37 records
implemented; `rack::engine::Engine` itself stays planned (partial) until 24B.
Validation: [rp-24a-validation.md](rp-24a-validation.md).

- **Handle.** `Context.engine` is one checked `Engine` handle per context.
  Every method is main-thread. Methods that Rack's `Engine.hpp` says lock
  release the GIL and hold RackPy's engine lease. Inside a callback Rack runs
  under the exclusive lock, they raise `EngineLockError` rather than deadlock.
- **Other plugins' modules.** `get_module` returns a RackPy module's own
  handle, or an `EngineModule`: an ID that is looked up again at every use,
  never Rack's pointer.
- **Ownership.** Rack's module widgets own their modules. So `add_module`
  takes only a Python-owned RackPy module with a model of this plugin, and
  `remove_module` only a RackPy module with no widget, no controls (tracked
  through external backreferences) and no cables. Python then owns it again
  (`Construction::reclaimFromRack`). `clear` and `from_json` delete what
  widgets own, so they are refused while the rack has module or cable
  widgets.
- **`stepBlock`.** Rack allows it only from the master module, so
  `step_block` needs a RackPy master module. The test shows exactly the
  requested frames advancing while Rack's own thread waits.
- **Params.** IDs are checked, because Rack indexes them unchecked. Smoothing
  needs a module in the engine, because Rack keeps the smoothed module's raw
  pointer.
- **Deferred.** The four `_NoLock` methods with `caller_holds` locking stay
  unreviewed and move to 24B.

**RP-24B — cables and parameter handles**
(`tk-rackpy-24b-cables-and-parameter-handles-071b51`). 30 records implemented,
and `rack::engine::Engine` is now complete. Validation:
[rp-24b-validation.md](rp-24b-validation.md).

- **Cables.** A Python-made `Cable` owns Rack's struct and holds module
  handles, not Rack's pointers. An engine cable is found again by its ID,
  because Rack deletes cables silently.
- **Cable endpoints.** Rack asserts in `removeModule` that no cable still
  touches the module, and a module widget's deletion goes through there. So
  `add_cable` joins only RackPy modules in the engine that no module widget
  owns (`widgets::moduleHasWidget`). `create_module_widget` is refused for a
  module that such cables touch, and `remove_cable` for a cable that a cable
  widget owns. Cables with widgets are RP-28E.
- **Rack's engine lock from native code.** Rack's public `resetModule` and
  `prepareSaveModule` lock and then call a module that need not be in the
  engine. A private native module (`underEngineLock`) therefore runs Rack's
  calls that need the lock held, such as `Cable::fromJson`, which uses
  `getModule_NoLock`.
- **Parameter handles.** Python's handles live in native slots that RackPy
  keeps while Rack's engine has them. Rack's `Engine::clear` removes every
  handle silently, and removing one that is gone asserts. A private sentinel
  handle, mapped to module ID 2\*\*53 (never assigned by Rack), detects the
  clear. Handles left in the engine are removed at context close.
- **`_NoLock` methods.** They are bound and allowed only where RackPy knows
  Rack holds the exclusive lock (`runtime::insideExclusive`).
- **Rack 2.6.6 quirk.** `updateParamHandle` to the param a handle is already
  on unmaps it. The binding keeps Rack's behaviour and documents it.
- **Test hook.** `call_after_ui_step` runs a callable from RackPy's
  scene-level step, which may clear Rack's rack and delete the RuntimeProbe
  widget that drives tests.

**RP-24C — history: undo actions and `Context.history`**
(`tk-rackpy-24c-history-undo-actions-and-context-hist-e2938d`). 91 records:
every public `rack::history` record, `Context.history`,
`appendDisconnectActions` and `getModuleDragAction`. Validation:
[rp-24c-validation.md](rp-24c-validation.md).

- **Python actions.** Every action type is a pybind11 `classh` trampoline
  (`py::init_alias`). An action pushed to Rack's State or a ComplexAction
  goes to Rack through pybind11's self-life support, which keeps its Python
  half alive, and `noteTransferred` counts it for shutdown. When Rack
  deletes it, the Python object has no native object left.
- **Rack's actions** are `ActionView`s, found again in their tree at every
  use. `type_name` is the C++ type.
- **Checks on Rack's own undo and redo.** `ParamChange` checks its param
  ID. `CableAdd`/`CableRemove` check port IDs and port widgets, using
  `portCableSafe` for Python ports. `ModuleAdd`/`ModuleRemove` need their
  model and JSON, and `ModuleChange` its JSON. A failed check does nothing,
  as Rack does for a missing module.
- **Re-entrancy.** Rack's `State::push` deletes the actions after the
  current one, which can include the running action. While an undo or redo
  runs, every history-pushing binding is therefore refused: `push`, the
  `*_action` methods, `do_action` and `ComplexAction.push`.
- **InverseAction** is a Python template class, built on the component
  library's `Template` (whose specializations now take the template's own
  module). Its swapped methods run Rack's code for native types through
  `_rack_undo`/`_rack_redo`, and the Python methods otherwise.
- **`State.undo`/`redo`** release the GIL and report Python actions' errors,
  as Rack's Edit menu does. A bound `action.undo()` raises them.
