# Drawing and the window

`rackpy.raw.nanovg` is NanoVG's drawing API, the one Rack's `DrawArgs` hands
widgets. `rackpy.raw.window` is `rack::window`: the OS window, its fonts and
images, SVGs and unit helpers. **Implemented (RP-27A, RP-27B), Linux x86_64 inside
Rack Free 2.6.6**, for every `rack::window` record and the Context fields
listed below (see the [coverage report](coverage.md)). Rack renders under
Xvfb with Mesa's software OpenGL in the tests, which compare pixels Rack
rendered with tolerances; no other GPU or driver is tested.

(window-drawing)=
## Drawing with NanoVG

Rack calls a widget's `draw(args)`, `draw_layer(args, layer)` or `draw_vg(vg)`
([widgets](widgets.md)). `args.vg` is a `GraphicsContext`, and every NanoVG
function takes it first, with NanoVG's names in snake case and without the
`nvg` prefix:

```{rackpy-example} drawing.py
```

- **Paths:** `begin_path`, `move_to`, `line_to`, `bezier_to`, `quad_to`,
  `arc_to`, `arc`, `rect`, `rounded_rect`, `rounded_rect_varying`,
  `ellipse`, `circle`, `close_path`, `path_winding`, then `fill` or
  `stroke`.
- **Style:** `fill_color`, `fill_paint`, `stroke_color`, `stroke_paint`,
  `stroke_width`, `miter_limit`, `line_cap`, `line_join`,
  `shape_anti_alias`, `global_alpha`, and Rack's additions `global_tint`,
  `get_global_tint`, `alpha` and `tint`.
- **Paints:** `linear_gradient`, `box_gradient`, `radial_gradient` and
  `image_pattern` return an immutable `NVGpaint`.
- **Colours:** `rgb`, `rgba` (0-255), `rgbf`, `rgbaf` (0-1), `hsl`, `hsla`,
  `lerp_rgba`, `trans_rgba`, `trans_rgbaf` return `NVGcolor`s and work
  anywhere. So do `rackpy.raw.color`'s helpers.
- **Transforms:** `translate`, `rotate`, `scale`, `skew_x`, `skew_y`,
  `transform`, `reset_transform` and `current_transform`. The pure helpers
  (`transform_identity`, `transform_multiply`, `transform_inverse`,
  `transform_point` and the rest) work on 6-tuples, as NanoVG's float arrays.
- **State and clipping:** `save`, `restore`, `reset`, `scissor`,
  `intersect_scissor`, `reset_scissor`, and the composite operations
  (`global_composite_operation` and the blend functions).
- **Text:** `font_face_id` (a `window.Font` or its handle), `font_face`,
  `font_size`, `font_blur`, `text_letter_spacing`, `text_line_height`,
  `text_align`, `text` (returns the x after the text) and `text_box`.
  Measuring: `text_bounds` returns `(advance, (xmin, ymin, xmax, ymax))`,
  `text_box_bounds` the box, `text_metrics` `(ascender, descender,
  line_height)`, `text_glyph_positions` a list of `NVGglyphPosition` and
  `text_break_lines` a list of `NVGtextRow`. Their indices count Python
  characters, not UTF-8 bytes.
- **Fonts and images by handle:** `find_font`, `add_fallback_font(_id)`,
  `reset_fallback_fonts(_id)`, `image_size`.
- The constants keep NanoVG's names: `NVG_CCW`, `NVG_HOLE`, `NVG_ROUND`,
  `NVG_ALIGN_CENTER`, `NVG_SOURCE_OVER`, `NVG_IMAGE_REPEATX`, `NVG_PI` and so
  on.

A `GraphicsContext` draws only during its own callback, and only while that
callback is the innermost one. A context kept after the call raises
`StaleHandleError`. Using an enclosing callback's context while Rack draws a
child raises `OwnershipError`, as does drawing with a context from a
non-drawing event (`on_context_create`). `save()` calls a callback leaves
unrestored are restored when it ends, and `restore()` without a matching
`save()` raises `OwnershipError`, because it would pop Rack's own state.

These are refused because NanoVG itself would crash: `font_size` outside 0 to
3276, a fallback font handle that no loaded font has, and
`reset_fallback_fonts` with an unknown name all raise `ValueError`. An
unknown image handle draws nothing, as in NanoVG.

Rack owns the frame and the contexts, so `nvgBeginFrame`, `nvgEndFrame` and
`nvgCancelFrame` are not bound. Fonts and images are created through the
window below, so NanoVG's own `nvgCreate*`, `nvgUpdateImage` and
`nvgDeleteImage` are not bound either. Neither are its debugging functions.
OpenGL itself is not bound: an `OpenGlWidget` can override `draw_framebuffer`
and draw with NanoVG.

(window-facade)=
## The drawing facade

`rackpy.drawing` is a more Pythonic shape for the same drawing. It adds no
behaviour of its own:

```{rackpy-example} painter.py
```

- `Painter(args)` (or `Painter(vg)` in `draw_vg`) wraps the callback's
  `GraphicsContext`, and expires with it as the context does.
- `saved()` and `clipped(x, y, width, height)` are context managers around
  `save`/`restore` and the scissor.
- `translate`, `scale`, `rotate` and `alpha` return the painter, so they
  chain.
- `path()` starts a `Path2D` whose methods chain and end in `fill(paint)` or
  `stroke(paint, width=)`. `rect`, `circle` and `line` start one with a
  shape; `hole()` makes the last shape cut out.
- A colour is an `NVGcolor`, a `"#RRGGBB"` or `"#RRGGBBAA"` string, or a
  tuple of 0-1 floats. `drawing.color()` converts one and, unlike Rack's
  `fromHexString`, raises `ValueError` for bad text. The gradient methods
  take colours too.
- `text(text, x, y, size=, color=, font=, align=, width=)` draws text, and
  wraps it at `width` if given. `measure()` returns a `TextBounds`. `font` is
  a `Font` or a path, absolute or in Rack's system folder (`DEFAULT_FONT` is
  Rack's UI font). `align` is a horizontal word (`left`, `center`, `right`)
  and a vertical one (`top`, `middle`, `bottom`, `baseline`).
- `image(image, x, y, width, height, alpha=)` draws an `Image` or an image
  file.

Fonts and images named by path are loaded through the window on every call,
as Rack advises. A missing file raises `FileNotFoundError`.

(window-context)=
## The context

`rackpy.raw.context_get()` (or `APP()`) is Rack's context on Rack's main
thread ([input](ui.md#context-and-input)). Its `window` is the `Window` below, or
`None` in headless Rack. `patch` and `midi_loopback_context` are opaque
`NativeHandle`s, because their headers are outside `rack.hpp`. Its engine,
scene and history come with their own families.

`context_set(context)` is Rack's `contextSet()`. A thread Python starts has
no context, so `context_get()` returns `None` there. After
`context_set(context)` it returns the context, as in a C++ plugin thread
that calls `contextSet(APP)`. Main-thread-only APIs still refuse to run on
such a thread. On Rack's main thread, `context_set` accepts only the context
that is already current. `app_get()` is Rack's deprecated `appGet()`.

A `NativeHandle` stands for a native object Rack owns: an `NVGcontext`, the
`GLFWwindow`, an `NSVGimage`, the patch manager. It has a `kind` and an
`alive` flag, and nothing else. The same object gives the same handle for as
long as it lives.

(window-window)=
## The window

`Window`, the context's `window`, works on Rack's main thread:

- `get_size()` and `set_size(size)`, which Rack clamps to at least 640 x 480.
  The window system applies a new size asynchronously.
- `set_full_screen()` and `is_full_screen()`; `cursor_lock()`,
  `cursor_unlock()` and `is_cursor_locked()` (if Rack's settings allow it).
- `get_mods()`, the modifier keys held now (`RACK_MOD_*` bits). In an event
  handler, use the event's `mods` instead.
- `get_monitor_refresh_rate()`, `get_frame_time()`,
  `get_last_frame_duration()` and `get_frame_duration_remaining()`.
- `close()` asks Rack to close its window after this frame: Rack quits.
- `pixel_ratio` and `window_ratio` are Rack's fields. Writes must be finite
  and positive, and Rack recomputes both every frame.
- `ui_font` is the font Rack's UI draws with every frame. Assigning it takes
  a loaded font of this window, never `None`.
- `vg`, `fb_vg` and `win` are `NativeHandle`s. Draw through a callback's
  `GraphicsContext`, not through them.
- `load_font(path)`, `load_font_without_fallbacks(path)`, `load_image(path)`
  and `load_svg(path)` load and cache resources. They return `None`, with a
  warning in Rack's log, if a file cannot be loaded.

(window-fonts)=
## Fonts

A `Font` from `Window.load_font()` is Rack's cached font, with Rack's
Japanese, Chinese and emoji fallbacks. `handle` is NanoVG's font number and
`vg` the context it was loaded in, both read-only. `Font()` makes an unloaded
font that Python owns, and `load_file(path, window.vg)` loads it, raising
`rackpy.raw.Exception` on failure. Rack's own fonts refuse `load_file`.
`Font.load()` is Rack's deprecated spelling of `Window.load_font()`.

(window-images)=
## Images

An `Image` from `Window.load_image()` is Rack's cached image. Draw it with
`nanovg.image_pattern(vg, ..., image, alpha)`. Rack loads images repeating
and linearly filtered. `Image()` and `load_file(path, window.vg)` make an
image Python owns. Its texture is deleted when Python drops it, on Rack's
main thread: at once there, or at the next UI frame if a Python thread
dropped it. `Image.load()` is deprecated, as `Font.load()` is.

(window-svg)=
## SVGs

`Svg` loads with `load_file`, `load_string` or the cached `Svg.load()`
([widgets](widgets.md#typed-bases)). `Svg.draw(vg)` draws it in a drawing
callback. `Svg.handle` is the parsed image as a `NativeHandle`, which goes
stale when `load_file` or `load_string` replaces it, and
`window.svg_draw(vg, svg.handle)` is Rack's `svgDraw`. `window.SVG` is
Rack's deprecated name for `Svg`, and using it warns.

(window-units)=
## Units

`window.SVG_DPI` (75) is Rack's pixels per inch and `window.MM_PER_IN`
is 25.4. `in2px` and `mm2px` convert a float or a `Vec`.

(window-frames)=
## Frames, framebuffers and layers

These are Rack's behaviours, tested over Rack's real frames:

- A `FramebufferWidget` renders its children only when it is dirty: at
  first, after `set_dirty()`, when it moves to a new subpixel offset (unless
  `dirty_on_subpixel_change` is off), or when its scale or visible area
  changes. Otherwise Rack keeps drawing the framebuffer's image. A bypassed
  one draws its children directly every frame. Rack may postpone a render to
  a later frame when a frame runs late.
- `RackWidget` draws modules on layer 0, dims the rack by the room
  brightness, then calls `draw_layer(args, 1)` for lights and halos, which
  therefore stay bright. A widget in a module widget can draw on layer 1 the
  same way. A `FramebufferWidget` renders layer 0 only.

The tests read the pixels of Rack's real window back, from the back buffer
that Mesa's software GLX keeps after a swap. The guide drawing in
`tests/rendering/reference/guide.png` is compared with tolerances, and was
reviewed by eye.

(window-lifetimes)=
## Lifetimes

Rack advises reloading fonts and images every frame rather than keeping them,
because they belong to the window's graphics context. Rack's cache makes
loading again cheap. RackPy holds Rack's cached fonts and images weakly. Every
`Font`, `Image` and NanoVG `NativeHandle` goes stale when Rack destroys its
graphics context, and using it then raises `StaleHandleError` ("load it
again"). Before Rack deletes the context, RackPy deletes the textures of the
images Python owns, so an `Image` Python keeps longer never touches a deleted
context. Rack Free destroys its graphics context only when it quits. The
tests also send Rack's destroy and create events through the scene, as a
host that recreates its window would.

(window-build-macros)=
## Build macros

`GLEW_STATIC`, `GLEW_NO_GLU` and `NANOVG_GL2` configure how Rack's headers
include OpenGL and NanoVG. They have no meaning at run time, so Python has
no counterpart.
