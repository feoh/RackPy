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

## RP-25D: raw Widget visibility field

**Witan:** `tk-rackpy-25d-raw-widget-visibility-field-8a2f6e`.

Adds the public `Widget::visible` bool as a checked read/write property. The
adapter writes the field directly (no `setVisible`, `show`, or `hide`) to retain
its raw semantics, including bypassing Show/Hide events. Keep this mapping
planned while full Widget subclass/lifecycle semantics remain incomplete. Test
read/write and interplay with the event-triggering visibility methods on an
actual Rack UI thread; document the behavioral distinction. Do not expose
`requestedDelete` as a raw field. No project phase completion.

## RP-25 continuation: checked raw box field views

Executed under the existing RP-25 parent claim, without another task graph.
`WidgetNode.box` exposes checked direct-field assignment and live nested
`WidgetBoxView`/`WidgetVecView` locators. Views revalidate context/thread/lifetime
on every native access. They do not retain a root or its owning handle, but
survive collection of an attached child's wrapper. Whole-field replacement
updates existing aliases. Direct writes bypass layout events; copied
`get_box()` behavior is unchanged. Explicit `copy()` produces ordinary values
for remaining Rect/Vec operations and value-taking APIs. Full view method/
operator interoperability remains an open raw box mapping requirement, so the
exact field entry stays `planned`.

Validation includes native alias/replacement/ownership/invalidation checks and
actual-Rack nested reads/writes, source validation, stale/foreign/thread access,
attached-wrapper collection, root collection, copy/deepcopy and the executable
snippet. Stubs, exact manifest evidence/generated coverage and human docs are
updated with this checkpoint; no full Widget or phase completion is claimed.

## RP-25 continuation: native stepping and deferred deletion

Executed under the existing RP-25 parent claim. Adds checked `request_delete()`,
raw `requested_delete` read/write, and explicit native-base `step()` calls. The
parent step deletes flagged children, invalidates subtree handles/field views,
and recurses through hidden trees. A detached root's own request does not
self-delete; remove/re-add preserves the flag; direct cancellation preserves
ownership. No Python callbacks are added. All three exact mappings stay
`planned` because full virtual/subclass/scoped-event semantics remain open.
Native and actual-host tests cover pending/cancelled requests, recursion,
transferred requests, stale views, wrong context/thread, plus retained geometry
view invalidation during real Context shutdown. Docs/snippet and strict stubs
show that detached trees are not automatically scene-stepped. Local validation,
failed attempts, cleanup and remaining gates are recorded in
[continuation validation](rp-25-continuation-validation.md).

## RP-25 continuation: subclassable widgets, typed bases and scoped events

Executed under the existing RP-25 parent claim. `rackpy.raw.widget.Widget` is
Rack's Widget with a pybind11 trampoline, so Python subclasses receive `step`,
drawing (`draw`, `draw_layer`, the deprecated `draw(NVGcontext*)` as
`draw_vg`, `draw_framebuffer`), all 31 event handlers and the coordinate
queries. Errors are contained; void callbacks are not replayed and value
queries fall back to Rack. Ownership is single: Python while detached,
the native parent after a transfer, with Rack's ownership keeping the Python
peer alive and its deletion invalidating it. Tree mutation is refused during
callbacks. The seven typed bases (Transparent, Opaque, Zoom, Transform, Svg,
Framebuffer, OpenGl) are bound with their fields; `rackpy.raw.window.Svg` is a
partial window slice that SvgWidget needs (the rest of window is RP-27).
Event records keep Rack's prototype hierarchy: owned when created, read-only
and expiring when Rack passes them. Graphics resources die before Rack's GL
context, through a scene anchor that separates graphics teardown from context
shutdown. `attach_to_scene()` hands a widget to Rack's scene.

229 manifest records move to (or stay) **planned**, none implemented: the
module browser, module removal, window recreation, `EventState`, templated
lookups and key-name functions remain. Tests run in actual Rack's live UI,
including Python drawing driven by `FramebufferWidget.render()` and a
scene-attached widget receiving the real window teardown. Evidence:
[continuation validation](rp-25-continuation-validation.md).

## RP-25 continuation: module removal, browser previews and recreation

RuntimeProbe test hooks follow Rack's own code: `add_probe_module` as the
browser adds a module, `remove_probe_module` as `ModuleWidget::removeAction`
(GIL released, engine lease held), `render_browser_preview` as
`ModelBox::createPreview` and its deletion, and `recreate_graphics_context`
as a host that recreates its window (Rack's two context events through the
scene). Python children of a deleted module widget get one Remove event
while the parent exists and are then invalidated; a child of a null-module
preview is stepped, drawn and deleted; managed framebuffers are freed and
redrawn across a recreation even when an override skips `super()`. These are
hooks in the test-hooks build, not the real browser window or a recreating
host, which the acceptance's actual-host smoke still needs once Python module
widgets exist (RP-16, RP-28).

## Remaining RP-25 acceptance

The original parent retains an actual-host smoke of the real module browser
and module deletion with Python module widgets (after RP-16/RP-28),
`EventState`, the typed ancestor/descendant lookups, the `recurseEvent`
templates, the key-name functions and the raw `children` list as a mutable
container.
Resource/drawing integration belongs with RP-27, not silently omitted from
Widget semantics. Detached native tests do not prove any of those gates.
