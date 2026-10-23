# RP-25 bounded implementation slices

These supplement [RP-25 in TASKS.md](TASKS.md#rp-25--bind-widget-trees-transforms-and-scoped-widget-events);
Witan is authoritative for status and blockers. No child relaxes the original
parent acceptance criteria or completes the implementation phase.

## RP-25A: checked detached native widget tree ownership

**Witan:** `tk-rackpy-25a-checked-detached-native-widget-tree-o-79a09d`.

Bounded groundwork: native-only base Widget nodes, live UI context and main-thread
checks, stable handle identity, parent/children snapshots, copied box access,
native visibility/bounds, add/add-bottom/remove/clear/close. Native ownership
transfers on add/remove, while Python handles never keep a parent alive.
Reject cycles, double add and closing an attached child. Child wrappers can
be collected without deleting native children; parent deletion invalidates
retained descendants. Root-handle final release on a Python worker only flags
native UI collection. Context cleanup deletes roots before EventState dies.

Exact manifest scope: 18 entries, all **planned**, with partial-adapter evidence:
Widget record/destructor; fields box/parent/children/visible; methods getParent,
addChild/addChildBottom/removeChild/clearChildren/hasChild/getBox/setBox/
isVisible/setVisible/getChildrenBoundingBox/getVisibleChildrenBoundingBox.
No completed full-Widget mappings are claimed.

Validation: real SDK CTest native differential order/bounds and lifetimes,
actual Rack production startup refusal and test-hooks live UI; identity,
transfer/reparent, stale handles, wrapper/owner GC, invalid inputs, wrong-thread
access, worker final release, teardown and 1,000 churns. Strict stubs, facade,
runnable live-UI exercise and prose; hooks/typing/native/host/introspection/
coverage/docs. Leave commit and durable checkpoint.

Local checks and explicit skipped gates are recorded in
[RP-25A validation](rp-25a-validation.md).

## RP-25B: checked Widget geometry and spatial queries

**Witan:** `tk-rackpy-25b-checked-widget-geometry-and-spatial-q-8df27f`.

Depends on RP-25A and implements the 12 exact `rack::widget::Widget` method
records for position/size getters and setters, `hide`/`show`, ancestry,
absolute/relative offset and zoom, and viewport clipping. The raw surface uses
`WidgetNode`; optional ancestor pointers are nullable, context-checked live
handles, never integer addresses. SDK default infinite-viewport behavior is
preserved. This is only the base Widget implementation: no ZoomWidget subclass,
TransformWidget, mutable raw fields, event dispatch or Python callbacks. These
records remain `planned` pending complete Widget semantics. Test SDK differential
behavior and actual Rack UI thread/context, geometry changes, relative and world
coordinates, viewport clipping, null/stale/foreign ancestors, visibility and
wrong-thread failures. Update manifest/stubs/docs/snippet and run hooks, native,
host, coverage/introspection and docs checks. The RP-25 parent and implementation
phase remain open. Local evidence is recorded in
[RP-25B validation](rp-25b-validation.md).

## RP-25C: safe Widget sibling insertion

**Witan:** `tk-rackpy-25c-safe-widget-sibling-insertion-b4de86`.

Implements the exact `Widget::addChildAbove` and `Widget::addChildBelow`
records on the checked `WidgetNode` base adapter. A child must be detached, a
sibling must be a direct child of the same parent, and all handles must belong
to the same live context. Invalid tree operations are rejected before Rack
assertions; insertion allocation failure must roll back Rack's early parent
assignment without changing native ownership or order. These two mappings stay
`planned` because class/subclass/event semantics remain incomplete. Validate
SDK child order and allocation rollback natively, then actual-host wrong-context,
stale/invalid sibling, cycle, duplicate, worker/thread and live UI behavior.
Update stubs/docs/snippet/manifest/generated report and mapping; run relevant
hooks, typecheck, native/host/introspection/coverage/docs and commit with a
checkpoint. This child and RP-25 parent do not complete the implementation
phase.

## Remaining RP-25 acceptance

The original parent retains complete class-factory/subclass and lifecycle
semantics, raw live field/container mutations, add-above/below, traversal/
typed ancestor/descendant lookup, transforms/zoom/viewport, base class variants,
callback-scoped events and propagation/consumption, exception containment,
module-browser/null-preview/scene attachment and window-recreation teardown.
Resource/drawing integration belongs with RP-27, not silently omitted from
Widget semantics. Detached native tests do not prove any of those gates.
