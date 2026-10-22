# Detached widget trees

RP-25A supplies **WidgetNode**, a checked adapter around native base
`rack::widget::Widget` objects. It is available from `rackpy.widget` and
`rackpy.raw.widget`, inside Rack's **live UI context** only. Creation during
plugin startup raises `ContextError`: Rack has not created its event state yet.

This is groundwork, **not** the complete Widget binding. It cannot be
subclassed, inserted into Rack's scene or module browser, or used for drawing.
Python virtual callbacks, events, transforms, resources, mutable raw fields
and class factories remain RP-25/RP-27 obligations. All touched SDK entries
remain **planned** in the [coverage report](coverage.md); safe adapters do not
count as complete public mappings.

## One native owner

- `WidgetNode.create()` creates a detached root owned by the adapter.
- `parent.add_child(child)` transfers native ownership to the parent. The
  Python child handle remains usable, with the same identity, but does **not**
  keep the parent alive. `add_child_bottom` inserts at the front of Rack's
  child list; `add_child` appends.
- `remove_child(child)` requires a direct child and transfers ownership back
  to the adapter. The child, its descendants, and their existing handles stay
  alive; it can be added to another parent. Implicit reparenting is refused.
- `clear_children()` deletes descendants, invalidating all their handles.
- `close()` deletes a detached root and its descendants. It is idempotent
  while the context is live. Closing an attached child raises `OwnershipError`;
  remove it first. Double add, cycles and adding a widget to itself also raise
  `OwnershipError`, without changing the tree.
- Dropping a child wrapper does not delete the parent's native child. A later
  lookup returns a new wrapper; simultaneous live lookups are `is`-identical.
- Dropping the last root wrapper schedules deletion on the UI thread. A
  retained child or `children` snapshot cannot keep that root alive. Cleanup
  runs on the next UI step or checked tree access; surviving child handles
  then raise `StaleHandleError`. Explicit `close()` is preferable when timing
  matters. This includes final handle release on a Python-created worker.

Every native access, including lifetime queries, is main-thread-only
(`ThreadError` otherwise); context-bound operations reject a different or
closed context. On context shutdown, after Python's closing hooks stop its
workers, native cleanup deletes **all** roots while Rack's EventState still
exists. Handles retained by Python become stale. No widget destruction or
Python reference operation is added to the production audio path.

## Snapshots and native behavior

`parent` is a checked identity-preserving handle or `None`. `children` is an
immutable **snapshot tuple** of handles, not a write-through alias of Rack's
public list. It neither retains the parent nor promises the children will
remain alive.

`get_box()` returns a **copied** Rect; editing it does not alter the widget.
Call `set_box(rect)` to apply a layout change. `is_visible()` and
`set_visible(bool)` use Rack's native visibility methods.
`get_children_bounding_box()` and `get_visible_children_bounding_box()`
use Rack's SDK implementation, including its empty-tree infinities.
There are no Python lifecycle overrides in this adapter.

## Runnable UI exercise

This listing defines a function; call it from a live Rack UI callback, **not**
at import time. The actual-host widget suite executes the function in a
RuntimeProbe UI step. That test probe is not shipped in production builds.

```{rackpy-example} detached_tree.py
:caption: Detached native ownership and layout exercise
```

## Validation boundary

The native CTest `widget_tree` compares adapter ownership and bounds with
SDK widgets, tests 1,000 lifetimes, a real SDK list-allocation failure,
worker final release, 100 worker/shutdown races and context cleanup.
`tests/widget/test_tree.py` separately tests startup refusal in production
Rack and exercises identity, transfer/reparenting, failure atomicity, snapshots,
stale handles, wrong-thread access, wrapper collection and 1,000 owner churns
in actual Rack Free's live UI under Xvfb.

These are detached-tree tests, **not** proof of module-browser integration,
graphics/window recreation, event propagation or Python callback containment.
Those remain parent-task acceptance gates. macOS and Windows are untested.
