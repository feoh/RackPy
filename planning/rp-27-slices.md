# RP-27 bounded implementation slices

RP-27 (`tk-rackpy-27-bind-window-resources-nanovg-drawing-a-a1c4a9`) binds
NanoVG drawing, `rack::window` and framebuffer behaviour. It is split into
bounded children, each closed with a commit on `main`. Decisions were made
under the owner's standing instruction to follow the recommended option
without asking (2026-10-25).

**RP-27A — NanoVG drawing, Window, Font and Image**
(`tk-rackpy-27a-nanovg-drawing-window-font-and-image-3a1b29`). 61 records.
Every `rack::window` record and Context's `window`, `patch` and
`midiLoopbackContext` fields are implemented; `contextSet` and `appGet` too.
Validation: [rp-27a-validation.md](rp-27a-validation.md).

- **Drawing.** NanoVG's drawing API (policy 13; not in the denominator) is
  `rackpy.raw.nanovg`, with NanoVG's names in snake case. Every drawing
  function takes the running callback's `GraphicsContext`. Decision: a
  context draws only in its own callback while it is the innermost one, and
  its unrestored `save()`s are restored when the callback ends, so Python
  cannot unbalance Rack's `drawChild` state. Rack keeps frames, contexts,
  resource creation and NanoVG's debugging functions; OpenGL is not bound.
- **Crashes NanoVG does not guard.** Found by the tests: fontstash indexes
  fallback fonts without bounds checks, so `reset_fallback_fonts` with an
  unknown name segfaulted Rack. Fallback functions now take only fonts Rack
  has loaded in the current graphics context, and `font_size` is limited to
  fontstash's short-tenths range.
- **Fixed on the way.** An owned `DrawArgs` that copied a callback's `vg`
  returned a `GraphicsContext` with the record's own, never-ending scope, so
  the context would have outlived its callback once drawing existed. It now
  keeps the callback's scope.
- **Lifetimes.** Rack's cached fonts and images are held weakly; every
  Font, Image and NanoVG handle goes stale at the next graphics-context
  destruction. Python-owned `Image()` textures are deleted on the main thread
  (deferred if dropped elsewhere) and before Rack deletes the context: an
  Image held until Rack quits is stale and Rack exits cleanly.
- **Opaque handles.** `NativeHandle` (kind, alive, identity) for
  `NVGcontext`, `GLFWwindow`, `NSVGimage`, the patch manager and the MIDI
  loopback context.

Context's `engine`, `scene` and `history` stay with RP-24 and RP-28.

**RP-27B — drawing facade, real-frame layers and framebuffers, reference images**
(`tk-rackpy-27b-drawing-facade-real-frame-layers-and--e78214`). No new
records (the facade is convenience over RP-27A's mappings). Validation:
[rp-27b-validation.md](rp-27b-validation.md).

- **Facade.** `src/rackpy/drawing.py`: `Painter` (saved/clipped context
  managers, chained transforms), `Path2D` (chained shapes, fill/stroke),
  colours from NVGcolor/hex/tuples, gradients, text with fonts loaded through
  the window per call, `measure()`, images. Decision: no behaviour of its
  own; it expires with its callback exactly like the raw context.
- **Real frames.** A windowed scenario steps through Rack's own frames: a
  FramebufferWidget renders only when dirty (set_dirty, subpixel moves unless
  disabled, bypass), and a Python widget in a module widget gets layer 1
  after RackWidget's room dimming, read from the window's pixels.
- **Reference image.** `tests/rendering/reference/guide.png`, rendered by
  Rack and reviewed by eye; compared with tolerances.
- **Moved.** Light/dark panel integration goes to RP-30 (the
  `preferDarkPanels` setting) and RP-28 (Rack's themed panels), with
  comments on both tasks.

RP-27 is complete with RP-27A and RP-27B.
