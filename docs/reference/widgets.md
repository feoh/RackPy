# Subclassable widgets and events

`rackpy.raw.widget.Widget` is Rack's `rack::widget::Widget`, and Python can
subclass it. Rack calls the subclass's overrides (`step`, drawing, event
handlers and the coordinate queries) on its UI thread. The typed bases
`TransparentWidget`, `OpaqueWidget`, `ZoomWidget`, `TransformWidget`,
`SvgWidget`, `FramebufferWidget` and `OpenGlWidget` are Rack's own classes
too, with their fields and methods. This page is about these classes;
[detached widget trees](widget-trees.md) covers the older native-only
`WidgetNode` adapter.

**Implemented, Linux x86_64 inside Rack Free 2.6.6 (RP-25).** The records
these classes touch are **implemented** in the [coverage report](coverage.md).
Rack's input dispatcher is in [UI widgets and menus](ui.md#context-and-input), and
NanoVG drawing and `rack::window` are in [drawing and the window](window.md).
Module removal and null-module
browser previews are tested through test hooks that follow Rack's code; a
smoke test in Rack's real module browser with Python module widgets waits for
RP-28. The names below are the raw layer; there is no `rackpy.widget` facade
for them yet. Accessing Rack's deprecated `widget.SVGWidget` and
`ui.ScrollBar` aliases warns; use `widget.SvgWidget` and `ui.Scrollbar`.

## Creating and owning widgets

A widget can only be created on Rack's main thread once Rack's UI context is
live, so never during plugin start-up (`ContextError` there). A new widget
belongs to Python. Each widget has exactly one native owner:

- `parent.add_child(child)` hands `child` to `parent`, as do
  `add_child_bottom`, `add_child_above` and `add_child_below`. Rack's parent
  now deletes it. The Python object stays alive, with the same identity, for
  as long as the native widget does, even if Python drops every reference to
  it: `parent.children[0] is child`.
- `parent.remove_child(child)` and `child.remove_from_parent()` give it back
  to Python.
- `parent.children` is Rack's child list as a live, read-only sequence
  (`WidgetChildren`): `len()`, indexing, slicing, `in`, `index()` and
  `count()` see the list as it is now, it compares equal to a tuple of the
  same children, and `copy()` takes a tuple snapshot. Iterating walks the
  children present when the loop starts. It has no `append` or `remove`:
  change the list with the methods above, which keep each child's single
  owner. Once the parent is deleted the view raises `StaleHandleError`; it
  does not keep the parent alive.
- `close()` deletes a detached widget and its subtree. Closing a widget that
  has a parent raises `OwnershipError`; remove it first.
- When Rack deletes a widget (its parent is closed or cleared, or a parent's
  `step()` honours `request_delete()`), the Python object stays, but
  `alive` becomes `False` and every other access raises `StaleHandleError`.
- Adding a widget that already has a parent, to itself or to one of its
  descendants raises `OwnershipError` and changes nothing.
- A class may have only one of these native bases (`TypeError` otherwise);
  ordinary Python mixins are fine.

Widgets that RackPy did not create, such as Rack's scene, appear as
`HostWidget`: a checked, nonowning handle with `alive`, `parent` and
`get_box()`. It can be passed where Rack takes a widget pointer (an ancestor,
an event target) but never owned or deleted.

All access is main-thread only (`ThreadError`), and widgets of another
context are refused. When Rack's context closes, RackPy deletes every widget
it still manages and their handles become stale. If a worker thread drops the
last reference to a widget, deletion waits for the UI thread.

## Overrides that Rack calls

Rack calls an override instead of its own implementation; call `super()` to
get Rack's behaviour as well:

| Override | Rack calls it |
| --- | --- |
| `step()` | every frame, for widgets in the scene |
| `draw(args)`, `draw_layer(args, layer)` | when drawing, see [drawing](#drawing) |
| `draw_vg(graphics)` | after `draw(args)`: Rack's deprecated `draw(NVGcontext*)`; defining or calling it warns |
| `draw_framebuffer()` | `FramebufferWidget` and `OpenGlWidget` only, while rendering |
| `on_hover(event)` … `on_context_destroy(event)` | for each of Rack's 31 widget events |
| `get_children_bounding_box()`, `get_visible_children_bounding_box()` | layout queries |
| `get_relative_offset(value, ancestor)`, `get_relative_zoom(ancestor)`, `get_viewport(rect)` | coordinate queries |

The methods of the RackPy classes themselves, which is what `super()`
reaches, run **Rack's** implementation for that class. That is also how to
deliver an event yourself: on a plain `FramebufferWidget`,
`frame.on_button(event)` dispatches the click to its children by position,
as Rack would.

Exceptions never reach Rack. RackPy logs them, and then:

- for `step`, drawing and event handlers, Rack carries on without running its
  own implementation again (it is not replayed after a failed `super()` call);
- for the queries that return a value, Rack uses its own result.

While Rack is inside one of these calls, changing the tree (`add_child`,
`remove_child`, `clear_children`, `close`) raises `OwnershipError`, because
Rack may be iterating the very list being changed. Call `request_delete()`
instead, or change the tree afterwards. Stepping another widget from inside a
callback is refused for the same reason; only the current widget's
`super().step()` is allowed.

## Events

Event records mirror Rack's: `Widget.HoverEvent` (also
`rackpy.raw.widget.HoverEvent`) derives from `BaseEvent` and
`PositionBaseEvent`, `Widget.DragHoverEvent` from `DragBaseEvent`, and so on,
with Rack's fields in snake case (`mouse_delta`, `scroll_delta`, `key_name`,
`clip_box`).

A record you create, such as `HoverEvent(pos=Vec(3, 4), context=EventContext())`,
is yours and writable. A record Rack passes to a handler is read-only
(`TypeError` on assignment) and **expires when the handler returns**: keeping
it and using it later raises `StaleHandleError`. Field reads are copies, so
values you keep stay valid.

`EventContext` carries the state that propagation shares: `propagating`,
`consumed` and `target`. `BaseEvent.consume(widget)`, `stop_propagating()`,
`set_target()`, `get_target()` and `is_consumed()` are Rack's; without a
context they do nothing, as in Rack. A target that Rack later deletes raises
`StaleHandleError` instead of dangling. `PathDropEvent.paths` is a list of
strings; `HoverKeyEvent.is_key_command(key, mods)` is Rack's check.

### Finding widgets and forwarding events

Rack's member templates take a class where C++ takes a type argument:

- `get_ancestor_of_type(cls)` and `get_first_descendant_of_type(cls)` are
  `getAncestorOfType<T>()` and `getFirstDescendantOfType<T>()`: the nearest
  ancestor, or the first descendant depth-first in child order, that is a
  `cls`, else `None`. `cls` is one of the classes on this page or a Python
  subclass of one. The classes on this page match Rack's own widgets too (Rack's
  scene is an `OpaqueWidget`), which come back as `HostWidget`; a Python
  subclass only matches widgets with that Python class.
- `recurse_event(method, event)` and `recurse_position_event(method, event)`
  are Rack's `recurseEvent` and `recursePositionEvent`, for a container that
  dispatches an event to its children itself. `method` is the handler, such as
  `Widget.on_hover`, and is called on each child, topmost first, while the
  event propagates, so the children's overrides run. The position form skips
  invisible children and those whose box does not contain `event.pos`, and
  passes the position in the child's coordinates. A handler that does not
  match the event raises `TypeError`, where C++ would not compile.

`get_key_name(key)` and `get_key_command_name(key, mods)` are Rack's display
names for GLFW key codes (`"A"`, `"Ctrl+Shift+A"`, empty for control codes),
in Rack's language. They need only the main thread, not a window.

## Drawing

Rack draws a widget by calling `draw(args)` with a `DrawArgs` record:
`args.vg` is an opaque `GraphicsContext`, `args.fb` the target `Framebuffer`
or `None`, and `args.clip_box` the visible part in the widget's own
coordinates. Both handles expire with the call. Within a draw call you can
draw with [NanoVG](window.md#drawing-with-nanovg) or draw an `Svg`
(`svg.draw(args.vg)`), call `super().draw(args)` to draw the children, or call
`self.draw_child(child, args, layer)` as Rack's `drawChild` does.

`FramebufferWidget.render()` is Rack's off-screen rendering and calls the
children's `draw` overrides synchronously, so it also works outside Rack's
frame loop. It refuses to nest inside a draw call (`OwnershipError`), an
`oversample` that is not finite and positive (`ValueError`), and sizes beyond
Rack's integer range. `get_framebuffer()` returns a checked handle that
becomes stale when the widget re-renders, deletes its framebuffer, or Rack's
graphics context goes away; `get_image_handle()` is Rack's NanoVG image ID
(-1 before rendering), not an address.

When Rack destroys its graphics context (the window closes), RackPy deletes
the framebuffers of every widget it manages, even if a Python
`on_context_destroy` override did not call `super()`, and their
`get_framebuffer()` handles become stale. On a new graphics context they are
marked dirty so that they render again. Rack Free never recreates its window:
the tests check recreation by sending Rack's own two events through the
scene, as a host that recreates its window would.

## Typed bases

- `TransparentWidget` ignores position events (hover, button, key and text
  while hovered, scroll, drag hover, path drops): neither it nor its children
  see them. `OpaqueWidget` delivers them to its children, stops their
  propagation and consumes them itself unless a child did.
- `ZoomWidget` scales its children: `zoom` is the raw field, `set_zoom()` is
  Rack's setter, and positions and offsets go through the zoom.
- `TransformWidget.transform` is a live, checked view of Rack's six-float
  matrix (`copy()` returns a tuple); `identity()`, `translate()`, `scale()`
  and `rotate(angle, origin)` are Rack's.
- `SvgWidget.svg` is the raw field: assigning it does not resize the widget,
  while `set_svg()` and `wrap()` do. `SVGWidget` and `set_svg_legacy()` are
  Rack's deprecated spellings. `rackpy.raw.window.Svg` loads SVGs
  (`load_string`, `load_file`, `Svg.load`; see [SVGs](window.md#svgs)).
- `FramebufferWidget` has Rack's `dirty`, `bypassed`, `oversample`,
  `dirty_on_subpixel_change` and a checked `viewport_margin` view.
  `OpenGlWidget` derives from it.

## Adding widgets to Rack's scene

`widget.attach_to_scene()` appends the widget to Rack's scene, which then
owns it, so it takes part in Rack's frame loop (stepping, drawing, input)
like Rack's own widgets. `remove_from_parent()` gives it back. A widget
still in the scene when the window closes receives Rack's `ContextDestroy`
event, then RackPy deletes it with the rest of its managed widgets. The tests
check attachment, detachment and that teardown, not frame-loop delivery.

## Inside module widgets and browser previews

A Python widget can also belong to a module's panel widget (`ModuleWidget`).
Module widgets cannot be written in Python yet (RP-16 and RP-28), so the
tests use RackPy's test-only RuntimeProbe module to show what Rack does:

- **Deleting a module.** Rack removes the module widget from the rack and
  deletes it, without the GIL. The Python child gets one `on_remove` while its
  parent still exists, then Rack deletes it and its handle goes stale. Only
  after that does the engine remove the module.
- **Browser previews.** The module browser builds each preview from a
  `ModuleWidget` with no module, in a zoomed `FramebufferWidget`; it steps it
  once, renders it, and deletes it with the preview. A Python child is stepped
  and drawn there like anywhere else, so it must not assume a module exists.

## Runnable UI exercise

This listing defines a subclass and a function; call the function from
Rack's UI thread, **not** at import time. The actual-host widget suite calls
it from a RuntimeProbe UI step.

```{rackpy-example} widget_subclass.py
:caption: A Python widget receiving a click and drawing in Rack
```

## Validation boundary

`tests/widget/test_tree.py` runs the `tests/widget/in_rack` suite in actual
Rack Free's live UI under Xvfb. It covers ownership transfer and identity for
every base, subclass peers kept alive by Rack, error containment, refused
mutations, worker release, event propagation and consumption, expiring and
read-only callback records, every event handler's base implementation, the
typed bases' fields, SVG and framebuffer lifetimes, and Python drawing driven
by `FramebufferWidget.render()`. It also covers scene attachment, a scene
widget receiving the real window teardown, module deletion and a null-module
browser preview (through test hooks that follow Rack's code), a simulated
graphics context recreation, the typed lookups (including Rack-owned
ancestors), both event recursion templates and the key names. It does not
cover Rack's actual browser window, input from a real mouse or keyboard,
frame-loop delivery, macOS or Windows.
