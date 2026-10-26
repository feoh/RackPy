# RP-27A validation (Linux x86_64)

Date 2026-10-25, on `main` after af5e707. Pinned inputs: GCC 16.2.1, uv
0.9.18, Rack SDK and Rack Free 2.6.6 (061ccf63), CPython 3.14.9, pybind11
3.1.0. Host runs serialized with `flock /tmp/rackpy-host-tests.lock`.

| Command | Result |
| --- | --- |
| `bin/rackpy-dev build --werror --test-hooks --build-dir build/hooks` | builds; 5/5 CTests pass |
| `uv run pytest -q -m 'not dist and not inventory'` | 199 passed, 31 deselected (444 s) |
| `uv run pytest -q -m inventory` | 29 passed |
| `uv run pytest -q tests/rendering` | 1 passed: 20 tests in actual Rack's live UI, plus the teardown check |
| `uv run pytest -q tests/widget tests/ui tests/integration` | 35 passed (the scoping change did not break earlier drawing tests) |
| `prek run --all-files`, `uv run bin/generate_bindings.py check` | pass |
| `uv run sphinx-build -q -W --keep-going -n -b html docs build/docs/html` | 0 warnings |

The rendering tests draw in Python `draw()` callbacks that Rack's
`FramebufferWidget::render` calls. A test hook reads the framebuffer Rack
rendered back with `glReadPixels` (premultiplied RGBA, top row first) and
compares pixels with tolerances of 2 to 24 levels. They cover:

- fills, strokes, curves, hole winding and rounded corners;
- transforms, save and restore, scissoring and composite operations;
- linear, radial and box gradients, global alpha and Rack's tints;
- image patterns from an original generated PNG, sampled at texel centres;
- `svg_draw` of a parsed `Svg.handle`, and the stale handle after a reload;
- text drawing and measurement (bounds, metrics, glyph positions with
  character indices for non-ASCII text, line breaking);
- refusals of out-of-range font sizes, unknown fallback fonts and names;
- context scoping: retained, enclosing and non-drawing contexts, and
  unbalanced saves;
- window queries and controls, including full screen, cursor lock and
  `close()` (cancelled by a hook);
- cached and Python-owned fonts and images, missing files, `ui_font`;
- staleness after Rack's ContextDestroy and ContextCreate events;
- `context_set` on a Python thread;
- the runnable `docs/snippets/drawing.py`.

**Teardown:** a Python-owned Image, a cached Font, the Window and the Context
are kept until Rack quits. At `rackpy._bootstrap.stop` all report stale, the
log line `window-resources-teardown-ok` is checked, and Rack exits with
status 0. There is no negative control showing the crash without the fix.

**GPU and driver assumptions:** Mesa's software OpenGL 2 under Xvfb
(1600 x 1000 x 24), at whatever pixel ratio Rack chose (the test scales by it,
but only one ratio is exercised). Other GPUs, drivers and
platforms are not tested. No reference images are stored yet; they come with
RP-27B.
