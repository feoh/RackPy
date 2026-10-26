# Undo history

`rackpy.raw.history` is Rack's `rack::history`: undo actions and Rack's
undo history, reached as `Context.history`. **Implemented (RP-24C), Linux
x86_64 inside Rack Free 2.6.6**, for every public record (see the
[coverage report](coverage.md)).

(history-actions)=
## Actions

Every Rack action type is a Python class: `Action`, `ComplexAction`,
`ModuleAction`, `ModuleAdd`, `ModuleRemove`, `ModuleMove`, `ModuleBypass`,
`ModuleChange`, `ParamChange`, `CableAdd`, `CableRemove` and
`CableColorChange`. Their fields carry Rack's names in snake case. A
module's JSON (`module_j`, `old_module_j`, `new_module_j`) is plain Python
data or `None`. Actions hold IDs, never objects, because Rack deletes and
restores the objects they refer to.

Subclass `Action` or `ModuleAction` for a module's own undo steps, and
override `undo` and `redo`:

```{rackpy-example} history_action.py
```

The `undo` and `redo` of Rack's own types run Rack's code. Rack assumes
valid IDs, so RackPy checks first, and does nothing if a check fails, as
Rack does for a missing module:

- `ParamChange` checks its param ID.
- `CableAdd` and `CableRemove` check their port IDs and that the ports have
  widgets in the rack. Otherwise Rack would add the engine cable, then raise.
- `ModuleAdd` and `ModuleRemove` need their model and module JSON before
  creating a module.
- `ModuleChange` needs its JSON.

`_rack_undo` and `_rack_redo` run Rack's code for the type alone, never a
Python override. `set_module(module_widget)` and `set_cable(cable_widget)`
are Rack's: they fill the action from a module widget, Python's or Rack's,
or from Rack's cable widget. `is_cable` compares a cable widget's ports.

`ComplexAction` batches actions. `push(action)` appends an action that
Python owns, which the ComplexAction then owns, and Rack undoes the batch
backwards. `InverseAction[T]` is Rack's `InverseAction<T>`: a cached
subclass of `T` whose `undo` runs `T`'s redo and whose `redo` runs `T`'s
undo, using Rack's code for Rack's types (`ModuleRemove` is
`InverseAction<ModuleAdd>` in Rack) and the Python methods for Python's.

### Ownership

An action is Python's until it is pushed to Rack's history or to a
ComplexAction, which then own it (`owned_by_rack`). Pushing it again
anywhere is refused. Rack deletes actions it owns: those after the current
one when another action is pushed, and all of them when the history is
cleared. A Python object whose action Rack deleted has no native object any
more, and pybind11 raises on use.

An action Rack made (from its menus, keys and module actions) is shown as an
`ActionView`. A view never holds Rack's pointer: at every use it finds the
action again in its tree, Rack's history or the ComplexAction it came from,
and it raises `StaleHandleError` once Rack deletes the action. A view gives
`name`, `type_name` (the C++ type, such as `rack::history::ModuleMove`),
`module_id` and a ComplexAction's `actions`.

(history-state)=
## Rack's history

`Context.history` is Rack's `history::State`, on the main thread:

- `push(action)` adds an action Python owns, without running it: do the
  change, then push its action, as Rack does. Rack then deletes the actions
  that were undone.
- `undo()` and `redo()` are Rack's Undo and Redo. As from Rack's Edit menu,
  errors in Python actions are reported, not raised, and the GIL is
  released.
- `can_undo`, `can_redo`, `get_undo_name`, `get_redo_name`, `set_saved` and
  `is_saved` are Rack's.
- `actions`, `action_index` and `saved_index` are Rack's bookkeeping, and
  read-only. `actions` holds Python's own action objects and `ActionView`s
  of Rack's.

Rack's `State::push` deletes the actions after the current one, which can
include the action whose undo is running. So while an action's undo or redo
runs, every call that pushes to Rack's history raises `OwnershipError`:
`push`, `undo`, `redo`, the module and rack `*_action` methods, a menu item's
`do_action`, and filling a ComplexAction.

(history-ui)=
## Rack's UI helpers

- `ModuleWidget.append_disconnect_actions(complex_action)` adds a
  `CableRemove` to a ComplexAction that Python owns for each of a Python
  module widget's cables, and removes those cables, as Rack's
  `disconnect_action` does before pushing. Push it to keep the step.
- `HostRackWidget.get_module_drag_action()` returns a ComplexAction of
  `ModuleMove`s for the modules that moved since
  `update_module_old_positions()`. Python owns it.
