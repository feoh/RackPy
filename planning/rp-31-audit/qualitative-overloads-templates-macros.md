## Review: RP-31 step 2 qualitative audit A (overloads, templates, macros, deprecations)

I only read files: no edits, builds, tests, git or Witan calls. I checked the manifest at main @ c580b05 against upstream headers in `~/.cache/rackpy/src/Rack/include`. Everything below comes from reading source and tests. I ran nothing, so I can't say whether the cited tests currently pass.

**Overall: BLOCK RP-31 closure** until tasks A–D below exist as Witan gap tasks and RP-31 is blocked on them. Nothing is wrong at the merge level.

### What already holds up

- **Overload groups from `overload_same_tests.json`.** Every overload in these groups is reachable from Python and exercised by a test that reaches it by argument type:
  - `math.clamp`, `clamp_safe`, `euc_mod`, `sgn` (`test_math.py:19-27`, plus int/float cases in `test_reference_replay.py:122-143`)
  - the `Vec` operators (`test_math.py:95-114`), `Rect.contains` (`test_math.py:145-146`), `color.mult` (`test_color.py:42-43`)
  - the `simd` overloads `abs`, `ifelse`, `movemask`, `movemask_inverse`, `pow` (`test_simd.py:226-290`, `:536-557`) and `literals.f32`/`f64` (`test_common.py:56-57`)
  - `translate` both overloads, `system.join` including the variadic form (`test_system.py:56`), and `unarchive_to_directory` from bytes and from a path (`test_system.py:99,105`)
  - `set_panel` (`test_module_widget.py:65-79`), `set_zoom` (`test_rack.py:66,68`), `get_cable` by id and by ports (`rp28e_cables.py:75`, `rp28b_browser.py:233`), `dsp.convert` (`test_basic.py:286-310`)
  - the constructors of `Xoroshiro128Plus`, `Version` and `Int24`.
- **The ~150 constructor+record and destructor pairs.** Each is a single default constructor with no separate overload behaviour. I spot-checked these rather than reading every one.
- **Deprecated no-argument `Module` events.** These are `on_add()`, `on_remove()`, `on_reset()`, `on_randomize()` and `on_sample_rate_change()`. They are callable and warn (`module.cpp:752-763`, test at `test_lifecycle.py:139-142`). The decision that one Python handler covers both spellings is written in the records' notes.
- **Templates with owner-approved or clearly stated contracts:**
  - ring buffers, `Decimator`, `Upsampler`, `MinBlepGenerator`, `SampleRateConverter` (approved 2026-10-26, `planning/README.md:100-105`)
  - the `T*` filter, trigger and timer templates for `float` and `float_4`, and the `simd::Vector` specializations
  - the Port SIMD templates, which can only take `float_4`, with the reason given (`manifest.json:2658-2662`)
  - the `config*`, `create*`, `getAncestorOfType` and `recurseEvent` templates, which take a class argument
  - the componentlibrary templates, which are class factories (`manifest.json:91-118`).
- **Macros.** Every macro in the inventory has a reviewed disposition. `no_runtime_meaning` is justified for `BINARY*`, `CONCAT*`, `TOSTRING*`, `GLEW_*`, `NANOVG_GL2`, `PRIVATE` and the `SIMDE`/`_MM` macros (`manifest.json:1411-1450`). The semantic ones (`DEFER`, `ENUMS`, `LENGTHOF`, `*_ARGS`, the log macros, `DEPRECATED`) are in `macros.py`.
- **Deprecated aliases and overloads that warn correctly:**
  - the 34 `rack::event` aliases (`event.py:92-100`), the 7 `app.SVG*` typedefs (`app.cpp:653-667`), `window.SVG` (`window.cpp:115-124`), `dsp.VUMeter` (`_dsp.py:145-152`)
  - `ModuleWidget(module)`, `Module(n,n,n,n)`, `get_param_handle(module, id)`, `set_brightness_smooth(float, int)`
  - the `EventState.set_*` methods and the `*_legacy` SVG setters
  - `step()`, which warns both when called and when a subclass overrides it (`laboratory.py:199-220`).

### Findings

**P1-1 · Seven deprecated declarations are mapped but never emit `DeprecationWarning`** (breaks policy 5). The checker does not test for this at all: there is no `deprecat` in `bin/check_coverage.py`.

| Record | Binding | Deprecated in Rack | Notes |
| --- | --- | --- | --- |
| `alias:rack::ui::ScrollBar` (`manifest.json:78`) | `ui.cpp:223`, plain attribute | `Scrollbar.hpp:27` | `test_widgets.py:32` checks the alias is the same class but not the warning |
| `alias:rack::widget::SVGWidget` (`:79`) | `variants_svg.cpp:59`, plain attribute | `SvgWidget.hpp:28` | no test mentions `SVGWidget` |
| `method:rack::widget::Widget::draw(NVGcontext *)` → `draw_vg` (`:2543`) | `behavior.hpp:61-64`, `trampoline.hpp:114-126` | `Widget.hpp:154` | neither calling nor overriding it warns |
| `alias_template:rack::componentlibrary::LEDLightButton<type>` (`:84`) | `_componentlibrary.py:468` | `componentlibrary.hpp:885` | |
| `record:rack::componentlibrary::LEDSliderGreen` (`:2764`) | `_componentlibrary.py:482-486` | `componentlibrary.hpp:745` | the note says "deprecated there" |
| `field:rack::app::Scene::mousePos` | `rack.cpp:128-132` | `Scene.hpp:23` | comment deprecation |
| `field:rack::app::RackWidget::touchedParam` | `rack.cpp:170-181` | `RackWidget.hpp:25` | comment deprecation |

The two fields are outside the strict alias/overload scope, but the same policy applies.

**P1-2 · Three overloads can be reached but no test calls them.** Deleting their `.def` would not fail any test.
- `method:rack::math::Vec::mult(rack::math::Vec) const` (`manifest.json:2275`, bound at `math.cpp:144`)
- `method:rack::math::Vec::div(rack::math::Vec) const` (`:2263`, `math.cpp:146`)

  The replay's `vec.mult_v`/`vec.div_v` cases go through `a*b` and `a/b`, which reach `operator*`/`operator/` rather than the methods (`test_reference_replay.py:166-170`). Every `.mult(...)`/`.div(...)` call in the tests passes a scalar.
- `method:rack::widget::TransformWidget::rotate(float)` (`:2526`, `variants_transform.cpp:42`). Only `rotate(angle, origin)` is called (`test_variants.py:89`).

**P1-3 · Some template contracts still await an owner decision but are recorded as implemented, and nothing tracks them as release blockers.**
- Affected records: `class_template:rack::dsp::IIRFilter<value, value, type>` (`:123`), `MidiGenerator<value>` (`:124`), `MidiParser<value>` (`:125`), and `function_template:rack::dsp::polyDirect`, `polyEstrin`, `polyHorner` (`:1387-1389`), plus the constructor and method records that depend on them (e.g. `:275`).
- Their notes say "an owner review item", and `planning/README.md:106-108` lists them as "Still open".
- The checker and `docs/reference/coverage.md` have no notion of an open owner item, so the coverage report counts these as finished. Policy 10 and the release bar say the release stays blocked until the owner decides.

**P1-4 · `SharedLock` only accepts one mutex type, and the record doesn't say so.**
- Affected records: `class_template:rack::SharedLock<type>` (`:89`) and `constructor:...SharedLock<TMutex>($c0 &)` (`:142`).
- In Rack, `SharedLock<TMutex>` works with any type that has `lock_shared`/`unlock_shared` (`mutex.hpp:59-69`).
- The binding casts its argument to RackPy's tracked `SharedMutex` and nothing else (`mutex.cpp:160-164`). The note gives no instantiation set and no generic contract.

**P1-5 · `constructor:rack::WeakHandle::WeakHandle(void *)` (`:144`) is marked implemented but Python can't call it.**
- `rackpy.raw.WeakHandle` is a final view class with no constructor (`weakptr.cpp:41-50`).
- The record's own note says only Rack makes handles. That means the public behaviour is unreachable. By the acceptance criteria it should be `unrepresentable` with owner scope review, not counted as complete.

**P1-6 · Macros on preprocessor branches the Linux build skips have no manifest records.**
- Affected: `ARCH_MAC`, `ARCH_WIN`, `ARCH_ARM64` (`arch.hpp:8,13,16`), `fopen` and `fopen_u8` (`common.hpp:133-147`), and the macOS values of `RACK_MOD_CTRL`/`RACK_MOD_CTRL_NAME` (`event.hpp:13-16`).
- They appear only in `api-surface.json` `conditional_blocks` (`:40-255`). `check_coverage.py` never reads that section; only `inventory_sdk.py` uses it.
- RackPy already implements these (`common.cpp:151-165`, `macros.py:46-47`), but no reviewed record covers them. Policy 9 says a macro never silently leaves the denominator.
- This overlaps the recorded macOS/Windows inventory gap from step 1.

**P2-7 · Eight event-overload notes contain garbled Python names.** For example, "callable as rackpy.raw.engine.Module.add_event &)" should read `on_add(e)` (`module.cpp:739-749`). Affected lines: `manifest.json:2120, 2121, 2123, 2125, 2127, 2129, 2132, 2134`.

**P2-8 · Overloads merged into one Python function, with no note saying so:**
- `function:rack::string::toBase64(const std::vector<uint8_t> &)` (`:1266`). The binding only calls the pointer overload (`strings.cpp:227-232`), while the docstring says "both overloads". Behaviour is the same, since Rack's vector overload just forwards (`string.cpp:230-232`).
- `function:rack::string::convertFArg(const std::string &)` (`:1249`). This one never runs, because the binding passes `c_str()` (`strings.cpp:121`).
- `method:rack::WeakPtr::operator=(...)` (`:1490`) is mapped to `__copy__`. Rack's assignment means `set(other.get())` on an existing pointer (`weakptr.hpp:57-60`), which Python can do through `set`.

**P2-9 · Some function templates cover fewer types than C++. The notes say so, but the owner hasn't signed off:**
- `isEven`, `isOdd`, `isPow2` handle `int64_t` only (`:1400-1402`). Values of 2^63 or more raise `OverflowError` (`test_math.py:33-34`), although `isPow2<uint64_t>(1ull<<63)` is valid C++.
- `sgn` handles `int64_t` and `float` (`:1403`), so the `double` version's behaviour is unreachable.
- `random::get<T>` (`:1404`): Rack's primary template casts `local()()` to any `T` (`random.hpp:82-86`), but signed types are refused (`test_utilities.py:118`).

**P2-10 · Test gaps:**
- Only 2 of the 34 `event` aliases are checked (`test_menus_events.py:240-243`). Of the 7 `SVG*` aliases, only `SVGKnob`, `SVGPanel` and `SVGPort` are tested (`test_controls.py:444-446`).
- `macro:DEBUG`, `macro:INFO` and `macro:FATAL` (`:1422,1431,1428`) cite `test_logging`, but only `WARN` is called. Only `WARN`'s caller location is checked (`tests/services/test_utilities.py:21`).
- A non-default template type is never passed for `createMenu`, `createMenuLabel`, `createCheckMenuItem`, `createBoolMenuItem`, `createSubmenuItem` or `createIndexSubmenuItem`. Only `create_menu_item(item_type=)` is tested (`test_menus_events.py:158`).
- The deprecated `ModuleWidget(module)` is never tested succeeding inside a widget factory. Only the error case and `None` are covered (`test_module_widget.py:48-51`).
- The English fallback in one-argument `translate(id)` is never exercised (`test_utilities.py:92`).

### Proposed gap tasks (block RP-31 on A–D)

- **A. Deprecation warnings** (P1-1). Add warnings for the seven IDs:
  - `ScrollBar` and `SVGWidget`: module `__getattr__`, as `window.cpp:115-124` does.
  - `draw_vg`: warn when called and when a subclass defines it, as `laboratory.py:199` does.
  - `LEDLightButton` and `LEDSliderGreen`: warn on access.
  - `mouse_pos` and `touched_param`: warn on get and set.

  Then add a parametrized test over every manifest record whose inventory entry is `deprecated` or `comment_deprecated`, asserting the warning, and a `check_coverage` rule that fails when one has no warning.
- **B. Overload tests** (P1-2). Test `Vec.mult(Vec)` and `Vec.div(Vec)` through the method form, adding reference cases to `values_reference.cpp`. Test `TransformWidget.rotate(angle)` against Rack's matrix. Removing any of these `.def`s must fail a test.
- **C. Owner decisions for templates** (P1-3, P1-4, P2-9). Get owner rulings on the IIRFilter, MidiGenerator/MidiParser and poly* bounds, the `SharedLock` mutex type (or bind it generically over any object with `lock_shared`/`unlock_shared`), the int64-only integer templates, `sgn` for `double`, and `random.get` for signed types. Add a machine-checked owner-review gate so open items count as blockers.
- **D. Skipped-branch declarations** (P1-5, P1-6). Add records with per-platform availability for every declaration in `conditional_blocks`, and make the checker fail on any without a record. Set `WeakHandle(void*)` to `unrepresentable` with owner review. This can be folded into the step-1 platform inventory task.
- **E (P2, optional).** Fix the eight garbled notes, add notes for the merged overloads, and close the P2-10 test gaps.

### Commands for you to run

`uv run bin/check_coverage.py check`, then the in-Rack test suites cited above (they need an actual Rack host). I ran neither.
