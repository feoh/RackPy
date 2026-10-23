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

## Remaining RP-25 acceptance

The original parent retains complete class-factory/subclass and lifecycle
semantics, raw live field/container mutations, add-above/below, traversal/
typed ancestor/descendant lookup, transforms/zoom/viewport, base class variants,
callback-scoped events and propagation/consumption, exception containment,
module-browser/null-preview/scene attachment and window-recreation teardown.
Resource/drawing integration belongs with RP-27, not silently omitted from
Widget semantics. Detached native tests do not prove any of those gates.
