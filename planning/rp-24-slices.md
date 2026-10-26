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
