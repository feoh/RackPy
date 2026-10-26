# RP-27B validation (Linux x86_64)

Date 2026-10-25, on `main` after 8691a42. Pinned inputs as in
[rp-27a-validation.md](rp-27a-validation.md). Host runs serialized with
`flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q tests/rendering` | 2 passed: 27 in-Rack tests (facade, guide image, RP-27A's), and the real-frame scenario |
| `uv run pytest -q -m 'not dist and not inventory'` | 200 passed, 31 deselected (453 s); inventory 29 passed |
| `prek run --all-files`, `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | pass, 0 warnings |

**Real frames** (`tests/rendering/test_frames.py`). Windowed Rack under Xvfb
steps a script once per frame. Measured on the planning machine:

- the framebuffer rendered once in its first frame, then not at all for five
  frames;
- it rendered once more one frame after `set_dirty()`, and again after a
  0.5 px move;
- with `dirty_on_subpixel_change` off, it did not render after a move;
- bypassed, it drew five times in five frames;
- its image showed green (0, 255, 0) in the window.

Layer 1 was drawn after RackWidget's room dimming:

| Room brightness | Panel layer red | Layer 1 red | Panel layer, undimmed |
| --- | --- | --- | --- |
| 0.25 | 43 | 171 | 171 |
| 0.5 | 85 | | |

The panel layer scales with brightness: 43/171 is 0.25 and 85/171 is 0.50.
Layer 1 is not dimmed. The common ceiling of 171 is a fixed darkening that
Rack draws above the rack, most likely its first-launch tip window (inferred,
not checked), so the test compares ratios.

**Reference image.** The guide drawing (120 x 80) has a gradient panel, a
stroked box, a ring with a hole, a line and text, and a radial light drawn
on layer 1 after a room dimming. It is rendered through FramebufferWidget,
stored as `tests/rendering/reference/guide.png` and was reviewed by eye.
The test accepts at most 0.5% of pixels differing by more than 24 levels,
with a mean difference of at most 2. `RACKPY_UPDATE_REFERENCE_IMAGES=1`
rewrites it and fails, so a new image is reviewed before it is committed.

**Assumptions:** Mesa's software OpenGL under Xvfb at pixel ratio 1. The
window pixels are read from the back buffer, which Mesa's software GLX keeps
after a swap. Its front buffer reads as zeros. Hardware drivers, other pixel
ratios and other platforms are not tested, and text anti-aliasing could
differ between Mesa versions beyond the tolerance.
