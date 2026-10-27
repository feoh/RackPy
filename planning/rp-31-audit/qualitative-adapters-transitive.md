I found four P1 gaps that should block closing RP-31, and four P2 notes. Each P1 involves records marked `implemented` whose C++ meaning isn't actually reachable from Python, or that the inventory misses entirely.

Scope and limits: I read the files you listed plus the relevant bindings and tests at c580b05. I did not build, run tests or query Witan. There is an untracked file, `tests/coverage/test_mutations.py`, probably from another session; I did not review it.

## Review

**Correct (checked against the code):**
- **`json_t`:** every public inventory entry that uses `json_t` has a manifest record, including fields, return values and `pluginSettingsJ`. The converter (`native/runtime/json.cpp:21-130`) handles nesting depth, NUL characters, NaN and 64-bit overflow, and frees partial results on error. The in/out `mergeJson` calls return the merged value (`app/rack.cpp:203-208,505-509`, `app/cables.cpp:252-256`). History `moduleJ` fields handle reference counts correctly (`history/history.cpp:483-492`).
- **NanoVG:** every drawing call checks that it runs inside the current draw callback (`widget/events.hpp:102-111`). Both Rack's NanoVG contexts and a callback's GraphicsContext are accepted by `load_file` (`drawing/resources.cpp:166-174`). `svg_draw` is checked; fonts are validated for fontstash. Framebuffer images are reachable through `get_image_handle`.
- **`void*`:** expander messages are handled by who allocated them (`manifest.json:759,763`). `Plugin::handle` and `Context::patch` / `midiLoopbackContext` are opaque, checked handles, as policy 4 requires.
- **`std::function` helpers** (`raw/_helpers.py`) match Rack's helpers.hpp line for line: check marks, alwaysConsume, submenu arrows, index submenus.
- **SharedMutex/SharedLock** refuse the misuses that would make Rack assert, and release the GIL while waiting. Plugin hooks (`loader.cpp:87-122`), `contextSet`, `APP` and `Exception` look sound.
- **Stubs:** nothing in `src/` or `native/` raises `NotImplementedError`.

---

**Finding P1-1 — a Python subclass of `rack::Quantity` cannot override the value methods, but the evidence only applies to `ParamQuantity`.**
- **Records:** `method:rack::Quantity::getValue()`, `setValue(float)`, `getMinValue()`, `getMaxValue()`, `getDefaultValue()` (`manifest.json:1458,1459,1464,1477,1453`).
- **What happens:** `src/rackpy/raw/__init__.py:106-124` raises `TypeError` for any `Quantity` subclass that defines these. The trampoline only calls Python when the laboratory flag is set (`support/quantity.hpp:55-83`).
- **Why the evidence doesn't fit:** the only justification (`upstream/callback-rules.json:106-116`) is Core MIDI-Map calling `ParamQuantity` on the engine thread. A plain `Quantity` only reaches Rack through `ui::Button`, `ui::ProgressBar` and `ui::Slider::quantity` (`api-surface.json:1435,1452,1467`), all on the UI thread (`ui/ui.cpp:106-121`).
- **What's lost:** Slider drags go through `moveScaledValue` → `setValue`. So the common C++ pattern of a custom Quantity driving a slider is impossible; `ValueQuantity` plus polling is the only option.
- **Smallest fix:** move the refusal to `rackpy.raw.engine.ParamQuantity.__init_subclass__`. Python's `ParamQuantity` inherits from `Quantity` (`standalone_quantity.cpp:221`), so the refusal must stay on that subclass. Then enable Python dispatch for plain-`Quantity` trampolines, calling only on the main thread and falling back to Rack's base method elsewhere. Reclassify the five methods as `ui` for `rack::Quantity` only.

**Finding P1-2 — GLFW key, mouse-button and action constants are neither mapped nor counted in the inventory.**
- **The rule:** policy 13 (`docs/coverage.md`) explicitly requires "GLFW key and modifier constants used by events".
- **What exists:** only the `RACK_MOD_*` modifiers and `RACK_HELD` (`raw/macros.py:41-52`). No `GLFW_KEY_*`, `GLFW_MOUSE_BUTTON_*` or `GLFW_PRESS`/`GLFW_RELEASE`/`GLFW_REPEAT` exist anywhere in `src/` or `native/`.
- **Evidence:** the repo's own tests hard-code them (`tests/ui/in_rack/test_menus_events.py:19`, `tests/app/in_rack/test_rack.py:28`, `tests/app/in_rack/test_controls.py:26`). The inventory's `boundary_types` lists only types (`api-surface.json:2-38`), so the constants never entered the count. That is a denominator gap, not just a usability one.

**Finding P1-3 — audio port process callbacks do nothing in production on Rack's real drivers.**
- **Records:** `method:rack::audio::Port::processBuffer(const float *, int, float *, int, int)`, `processInput(const float *, int, int)`, `processOutput(float *, int, int)` (`manifest.json:1840-1842`).
- **What happens:** on Rack's driver thread, Rack's base no-op runs and only `stream_stats` counts frames (`docs/reference/audio.md:45-58`; `planning/trampoline-reentry-validation.md:153`: "Native production processing remains counting/no-op"). Python overrides run only on devices Python drives itself.
- **Why it matters:** there is no native kernel or graph node connecting a port's buffers to a module, and nothing in `native/dsp`. So a Python plugin cannot build an audio-interface module. Policy 11 calls for a native production path, and a no-op isn't one.
- **Related:** `method:rack::midi::InputQueue::tryPop(rack::midi::Message *, int64_t)` is main-thread only (`manifest.json:2309`). Python modules therefore cannot pop MIDI with frame timing in `process()`.
- **Decision needed:** a C++ kernel library (`include/rackpy/kernel.hpp`) is "native only", which AGENTS.md says doesn't count. This needs an owner decision or new native nodes.

**Finding P1-4 — many settings records cite a test that never touches them.**
- **Records:** `variable:rack::settings::*` for `devMode`, `restart`, `safeMode`, `windowMaximized`, `windowPos`, `invertZoom`, `mouseWheelZoom`, `pixelRatio`, `cableOpacity`, `cableTension`, `spotlightBrightness`, `spotlightRadius`, `haloBrightness`, `allowCursorLock`, `knobScroll`, `knobLinearSensitivity`, `knobScrollSensitivity`, `sampleRate`, `lockModules`, `squeezeModules`, `autosaveInterval`, `skipLoadOnLaunch`, `lastSelectionDirectory`, `cableAutoRotate`, `autoCheckUpdates`, `verifyHttpsCerts`, `showTipsOnLaunch`, `browserZoom`.
- **Evidence:** they cite `test_settings_read_and_write` (e.g. `manifest.json:3026,3024,3049,3050`), which never mentions them (`tests/services/in_rack_settings/test_settings.py:49-78`).
- **Existence-only:** `token`, `uiTheme`, `lastPatchDirectory`, `tooltips`, `cpuMeter`, `frameRateLimit` and `tipIndex` are only checked with `is not None` (line 78).
- **Consequence:** swapping two macro lines in `settings.cpp:118-170` would pass every test. RP-31's mutation-test validation would not catch it.

---

**P2-1 — `constructor:rack::WeakHandle::WeakHandle(void *)` is `implemented`, but Python can't call it.** The `WeakHandle` class has no constructor (`widget/weakptr.cpp:41-52`; `manifest.json:144`). The note's reason is not a disposition policy 11 allows. Either add a reviewed disposition (owner review) or mark it unrepresentable.

**P2-2 — `WeakPtr` and `WeakBase` work only for widgets, with no owner contract.**
- `class_template:rack::WeakPtr<type>` is mapped only as `WeakPtr<Widget>` (`weakptr.cpp:17`). `record:rack::WeakBase` and `~WeakBase` point to `rackpy.raw.widget.Widget` (`manifest.json:2695,353`).
- `WeakBase` is a public, subclassable struct (`api-surface.json:3263`); C++ plugins can use it for their own types. Policy 10 requires an owner contract for that template space.

**P2-3 — `field:rack::ui::Menu::cornerFlags` is a bare int.** The `BND_CORNER_*` constants aren't exported (`ui/ui.cpp:296-298`).

**P2-4 — some container properties return copies but are typed as mutable.**
- `MultiLightWidget.base_colors` (`app/app.cpp:452-456`, `app.pyi:91`) is affected, along with `settings.cable_colors`, `cable_labels` and `recent_patch_paths` (`settings.pyi:98-99`), and `PluginWhitelist.module_slugs` (`settings.pyi:51`).
- They return copies but are typed `list`/`set`, so `.append()` silently changes nothing. Policy 8 asks for explicit write-through. Smallest fix: type them as `Sequence`/`frozenset`, or return write-through views.

**Note (outside the count):** Python can't create NanoVG images from pixel data (`nanovg.cpp:248`). That's intentional, and NanoVG's own API isn't in the count, but it rules out pixel-buffer displays such as scopes.

---

**Proposed gap tasks** (block RP-31 on each):

- **RP-31A — Quantity value overrides for UI-only quantities**
  - *Records:* the five records in P1-1.
  - *Contract:* main thread only. Off the main thread, Rack's base answers and a diagnostic is recorded. Subclassing `ParamQuantity` stays refused.
  - *Tests:* in-rack, a `ui.Slider` drag calls a Python `set_value`; a `ParamQuantity` subclass still gets `TypeError`; an off-thread call falls back. A mutation test removes the dispatch and must fail.
- **RP-31B — GLFW input constants as boundary entries**
  - *Scope:* extend RP-05's inventory with the GLFW macro values that events use (keys, mouse buttons, actions), seed manifest records, and export them as constants (e.g. a `rackpy.raw.glfw` module).
  - *Contract:* per-platform values, available on any thread.
  - *Tests:* compare against the headers' values, and update existing tests to import the constants.
- **RP-31C — Owner decision on engine-thread audio/MIDI I/O for Python modules**
  - *Records:* `audio::Port::processBuffer` / `processInput` / `processOutput`, and `midi::InputQueue::tryPop`.
  - *Options:* native graph nodes (a lock-free ring between port and module, on the engine thread), accept kernel libraries, or mark them unrepresentable. Full release stays blocked until decided.
  - *Tests:* a live Rack check that the virtual driver's samples reach a module's output with the same timing as C++.
- **RP-31D — Behavioural tests for each setting**
  - *Records:* the ~35 in P1-4.
  - *Contract:* main thread.
  - *Tests:* write a distinct value to each setting and read it back through a native fixture (`rack::settings::X`, not the Python getter), so swapped fields fail.
- **RP-31E — Owner review of weak pointers and `WeakHandle`**
  - Covers P2-1 and P2-2: settle `WeakHandle(void*)`, `WeakPtr<T>` and `WeakBase`.

**Merge verdict: BLOCK** — RP-31 shouldn't close while P1-1 to P1-4 are open. P1-3 needs your scope decision.
