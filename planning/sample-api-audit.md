# Sample-led Python API gap audit

Scope: user request to use the remaining official Rack samples to find the
highest-impact Python API gaps. This audit checks the current sample corpus and
its evidence; it is **not** a full review of Rack's public API.

## Result

No unported official plugin-development sample or sample-blocking operation was
identified in this pass. RP-33 through RP-36 are closed. `upstream/examples.json`
has 77 entries; each tutorial, API-guide and helper/template entry has a
disposition with implementation files and tests. Four entries (`building-06`
through `building-09`) have no disposition because they are explicitly about
building Rack itself, not writing a Rack plugin. On 2026-11-04,
`bin/manual-sources check` confirmed that all 69 captured code blocks still
match the live official manual.

The sample-path mapping and its main boundaries are:

| Workflow | Python-facing work / native boundary | Existing evidence | Gap found |
| --- | --- | --- | --- |
| Plugin template | Create a project, generate a module from an original SVG panel, build/register the plugin | `tests/examples/test_template.py`; production plugin loads under its slug | None found in this workflow |
| Tutorial oscillator | Python module/configuration and graph definition; native graph kernel performs per-sample DSP | `tests/examples/test_oscillator.py`; actual Rack install/patch-reopen checks | None found; owner also manually heard the preview's tone |
| Configuration | Parameter/display configuration, ports/lights, switch/button and bypass, including a native custom bypass kernel | `tests/examples/in_rack_controls/test_controls_in_rack.py` compares behavior with the C++ reference | None found in exercised behavior |
| State and events | Synchronous lifecycle/state callbacks, JSON, patch storage and plugin settings; sample-rate handling stays native on the audio thread | `tests/examples/test_controls_state.py` and `tests/examples/in_rack_controls/test_controls_in_rack.py` | Python is deliberately not called from the production audio callback |
| Menus | Rack context-menu items/submenus and callbacks, including browser/module lifetime cases | `test_every_switchboard_menu_item_works` in the in-Rack controls suite | None found in exercised behavior |
| Panels and drawing | Original light/dark SVG panels, NanoVG drawing, fonts, self-lit layers and framebuffer widgets | `tests/examples/test_drawing.py` with Rack-rendered frames | No sample feature gap found; hardware-GPU coverage is separate |
| Polyphony/SIMD | `float_4` and `rackpy.raw.simd` are available for bindings/lab use; production SIMD processing is a native kernel | `tests/examples/in_rack_dsp/test_dsp_in_rack.py` compares scalar/SIMD kernels with C++; `tests/examples/test_dsp_expanders.py` covers teaching parity | No missing sample operation found. Python does not execute SIMD kernels on Rack's audio thread |
| DSP extension | Rack DSP filters are used in a native kernel; Python version is teaching/offline only | The DSP example's tests compare module output with the Rack DSP reference and run audio-path guards | No sample-required API gap found; this example extends beyond the official sample inventory |
| Expanders | Neighbor/model checks and double-buffered messages; Rack's one-frame handoff is preserved | In-Rack expander tests verify source/receiver timing and native-kernel safety | None found in exercised behavior |

The official SIMD example's concrete calls `rack::simd::pow`, `floor` and `sin`
are implemented in the RackPy SIMD binding/stub (`src/rackpy/raw/simd.pyi`).
The SIMD sample calls Rack's C++ API in a native kernel because production DSP
must not call Python per sample. The separate Python teaching path is explicitly
not a real-time kernel. Likewise, the sample-rate event example demonstrates
Python handling in the offline laboratory while production handling stays
native. These are intentional boundaries, not hidden Python callback support.

## Representative API-to-sample traces

These spot traces verify the path through the binding manifest, sample source and
tests; they are not an exhaustive manifest review:

| Samples | Rack API / manifest record | Python mapping | Binding and sample evidence |
| --- | --- | --- | --- |
| `guide-02`–`guide-04` | `Module::configParam<type>`, `configSwitch<type>`, `configButton<type>` | `rackpy.engine.Module.config_param`, `.config_switch`, `.config_button` | `native/bindings/engine/module.cpp`; `tests/engine/module/in_rack/test_transferred_quantities.py::test_factories_match_native_reference`; sample comparison in `tests/examples/in_rack_controls/test_controls_in_rack.py` |
| `guide-13`–`guide-16` | `Module::getLeftExpander()`, `Module::leftExpander`, `Module::Expander::requestMessageFlip()` | `rackpy.engine.Module.get_left_expander`, `.left_expander`, `rackpy.engine.expanders.Expander.request_message_flip` | `native/bindings/engine/expander.cpp`; `tests/engine/expanders/in_rack/test_expanders.py`; one-frame sample behavior in `tests/examples/in_rack_dsp/test_dsp_in_rack.py` |
| `guide-25`, `guide-30` | `simd::Vector<float, 4>`, `Port::getPolyVoltageSimd<type>`, `simd::pow`, `simd::sin`, `simd::floor` | `rackpy.simd.float_4`, `rackpy.raw.engine.Port.get_poly_voltage_simd`, `rackpy.simd.pow/sin/floor` | `native/bindings/simd/` and `native/bindings/engine/components.cpp`; `tests/simd/in_rack/test_simd.py`; `examples/polyphony/kernels/polyphony.cpp` plus its actual-Rack reference tests |

The sample test is an end-to-end consumer check, while the binding-level test
checks the mapped operation more narrowly. Current manifest statuses for these
records are `implemented`, not `verified`; the sample pass does not by itself
satisfy the independent per-record review required for a complete-coverage
claim. The inventory dispositions name files and tests but do not yet store an
explicit list of the binding IDs each sample exercises. A quick join of all 34
unique disposition test functions against `bindings/manifest.json` finds only
two manifest records that directly cite a sample-disposition test. Other
binding-specific tests provide evidence, but the missing direct links make a
fully automated impact ranking impossible. That traceability gap is more real
than an observed missing operation in these sample workflows.

## Platform evidence

`bin/check_coverage.py samples` was run against the uploaded test evidence from
[CI run 41763149030](https://github.com/feoh/RackPy/actions/runs/41763149030),
source commit `16ecf405932b857197406673bec1d16ff09115c5`:

| Platform | Passed dispositions | Skipped | Out of scope |
| --- | ---: | ---: | ---: |
| Linux x86_64 | 73 | 0 | 4 |
| macOS arm64 | 69 | 4 | 4 |
| macOS x86_64 | 69 | 4 | 4 |
| Windows x86_64 | 69 | 4 | 4 |

The four skips on each non-Linux target are the Linux-specific no-allocation /
no-lock audio-path guard tests for `guide-07`, `guide-13`, `guide-25` and
`guide-30`. Other functional sample tests passed. This is a platform-specific
guard-test limitation in that older CI evidence, not evidence of missing
sample features. It should only be expanded if broader target validation is
useful; it is not the next API feature to implement. This CI run validates its
own source commit, not later preview bytes. The later local Linux preview passed
a separate clean Ubuntu 20.04 candidate install/run suite.

## What remains unresolved

- The sample tests establish that these selected workflows worked on their
  tested paths; they do **not** establish complete API coverage, prove every
  overload, or upgrade `implemented` manifest entries to independently
  `verified`.
- The broader coverage report has remaining `absent` inventory counts. A
  comparison of surface IDs with the binding manifest found the unrepresented
  rows in this checkout were C++ `import`/`using` declarations, not missing
  method/class/field declarations. The commonly exercised SIMD functions above
  are still present in `rackpy.raw.simd`. Namespace parity still matters to a
  future full-surface claim, but this list did not reveal a high-impact sample
  blocker.
- There is therefore no sample-driven implementation task to reopen. For future
  user-reported gaps, link the concrete plugin workflow to the exact binding
  manifest record, failing operation and regression test; implement that
  bounded gap before widening the API review. Keep full-coverage claims and
  release gates separate from this practical sample audit.
