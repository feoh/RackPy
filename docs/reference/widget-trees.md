# Detached widget trees

RP-25A supplies **WidgetNode**, a checked adapter around native base
`rack::widget::Widget` objects. It is available from `rackpy.widget` and
`rackpy.raw.widget`, inside Rack's **live UI context** only. Creation during
plugin startup raises `ContextError`: Rack has not created its event state yet.

This is groundwork, **not** the complete Widget binding: a `WidgetNode`
cannot be subclassed, put in Rack's scene or used for drawing. For Python
subclasses, events, drawing, the typed bases and scene attachment, use
`rackpy.raw.widget.Widget`, described in
[subclassable widgets and events](widgets.md). The SDK records are mapped
through `rackpy.raw.widget.Widget`; this adapter shares them but, as a safe
adapter, does not count toward their coverage.

## One native owner

- `WidgetNode.create()` creates a detached root owned by the adapter.
- `parent.add_child(child)` transfers native ownership to the parent. The
  Python child handle remains usable, with the same identity, but does **not**
  keep the parent alive. `add_child_bottom` inserts at the front of Rack's
  child list; `add_child` appends. In Rack's child-list order,
  `add_child_above(child, sibling)` inserts immediately after the sibling and
  `add_child_below(child, sibling)` immediately before it. The sibling must be
  a direct child of the receiver; the new child must be detached. These calls
  never silently reparent it.
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

### Deferred native deletion and stepping

`child.request_delete()` calls Rack's `requestDelete()`, setting the raw
`requested_delete` bool. It does **not** immediately close the handle. During
`parent.step()`, Rack deletes flagged direct children (and their subtrees),
invalidating all retained handles and geometry views. For other children it
recurses, including invisible ones. A detached root's own flag does not delete
it when its own `step()` runs: only its parent can consume that flag. Use
`close()` for deterministic detached-root destruction.

`requested_delete` is also a checked read/write raw field. Assigning `False`
cancels a pending request. Removing a child from its parent does not clear its
flag; adding it to another parent lets that parent's next step delete it.
Detached trees are **not** scene-registered and do not advance automatically;
call `step()` explicitly from the UI thread when needed. This adapter calls
only the native base Widget implementation. Python `step` overrides, scoped
Remove events and callback error containment are not part of this adapter;
`rackpy.raw.widget.Widget` provides them.

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
Call `set_box(rect)` to apply an event-triggering layout change. For direct raw
field semantics, `box` is a checked write-through `WidgetBoxView`:

- `node.box = Rect(...)` assigns the native public field without calling setters.
- `node.box.pos` and `.size` return `WidgetVecView` aliases. Their `x`, `y`, and
  indices `0`/`1` write through. Assign a `Vec` or another checked vector view to
  `.pos`/`.size`; `+=` and `-=` also write through using Rack's Vec arithmetic.
- Existing views follow changes made by `set_box()` or whole-field replacement;
  they do not become stale copies of the previous geometry.
- `box.copy()` returns an independent `Rect`, and `box.pos.copy()` an independent
  `Vec`. `copy.copy()` and `copy.deepcopy()` also take checked snapshots. The
  views are **not** Rect/Vec subclasses: use these snapshots for other math
  operations or arguments to existing value-taking APIs. The views also
  forward Rect/Vec's read-only methods to a snapshot and convert implicitly
  where Rack takes a `Rect` or `Vec`.

Direct field writes bypass `setBox()` and therefore do **not** generate
Reposition/Resize events. Use `set_box()`, `set_position()`, and `set_size()` when
those events should occur. All view reads and writes require the live UI context
and main thread. A view can outlive an attached widget's Python wrapper, but
cannot keep its native widget or owning root alive. Owner collection, subtree
removal by deletion, or context shutdown invalidates it; subsequent access fails
with `StaleHandleError` or `ContextError`, never a borrowed-pointer dereference.
There is no public constructor for the view types. Cross-view assignment first
takes a checked value snapshot, so an invalid source does not change the target.

`is_visible()` and `set_visible(bool)` use Rack's native visibility methods.
`get_children_bounding_box()` and `get_visible_children_bounding_box()`
use Rack's SDK implementation, including its empty-tree infinities. Position and
size methods use Rack's setters, so real changes trigger its reposition/resize
events. `show()`/`hide()` likewise use Rack's visibility setter. The writable
`visible` property directly reads/writes Rack's public bool field and intentionally
bypasses `setVisible()` and its Show/Hide events; prefer `set_visible()` or
`show()`/`hide()` for event-triggering changes. `is_descendant_of` checks parent
ancestry (not direct-child membership).

`get_relative_offset(position, ancestor)` and
`get_relative_zoom(ancestor)` preserve Rack's coordinate-space semantics;
pass `None` explicitly for the root/world coordinate system (the raw SDK pointer
parameters do not have Python-default arguments). A non-null ancestor must be a live
WidgetNode in the same Rack context. The base adapter uses Rack's base Widget,
so zoom remains 1 unless a future subclass adapter explicitly supports ZoomWidget.
`get_absolute_offset()` and `get_absolute_zoom()` are Rack's convenience forms.
`get_viewport(rect=...)` defaults to Rack's infinite rectangle and returns the
portion clipped by the widget and its ancestors. These calls are checked main-
thread native operations; no Python lifecycle overrides are provided here.

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
stale handles, raw box/vector aliasing and snapshots, view invalidation,
wrong-thread/context access, wrapper collection and 1,000 owner churns
in actual Rack Free's live UI under Xvfb.

These are detached-tree tests, **not** proof of module-browser integration,
graphics/window recreation, event propagation or Python callback containment.
Those remain parent-task acceptance gates. macOS and Windows are untested.
