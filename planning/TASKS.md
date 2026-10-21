# Detailed task contracts

Generated from [tasks.json](tasks.json). These are future implementation contracts,
not claims of completion. Live ownership/status/dependencies are in Witan. All
tasks inherit the shared execution contract in tasks.json and AGENTS.md.

## RP-00 — Publish the planning repository and detailed execution graph

**Priority:** p1 · **Depends on:** none

### Deliverables

- `README.md`
- `docs/`
- `planning/`
- `examples/README.md`
- `Public GitHub repository and Witan task graph`

### Implementation steps

1. Research the supported Rack public plugin API and licensing; ask the owner to choose runtime model, license and this session's scope.
2. Create the public RackPy repository; write truthful planning documentation, architecture, coverage, performance, sample provenance and contribution contracts.
3. Create detailed task contracts and dependency edges in Witan; mirror all task text and slugs in git.
4. Validate hooks, JSON/DAG, local documentation links and remote visibility; commit and push.
5. Record the WorkflowProject schema blocker and recovery task; do not imply runtime/sample implementation exists.

### Validation

- prek run --all-files
- Validate planning JSON, references and acyclic dependencies.
- gh repo view feoh/RackPy --json url,visibility; compare pushed ref to local commit.

### Acceptance criteria

- Public repository contains the complete executable plan and GPL license.
- Detailed Witan tasks have verified parent/dependency edges.
- Repository is explicitly planning-only; future implementation stays open.

**Notes:** Owner renamed VCVPy to RackPy; initial epic/bootstrap Witan slugs retain old spelling.

## RP-01 — Recover Witan project metadata without duplicating the task graph

**Priority:** p1 · **Depends on:** none

### Deliverables

- `planning/witan.json`
- `Witan WorkflowProject and WorkflowSession links`

### Implementation steps

1. Read the epic and every existing RackPy task from Witan; compare their slugs with planning/witan.json before creating anything.
2. Check workflow_project_search once after an authorized service owner has repaired the missing WorkflowProject.preauthorized schema field. Do not mutate the shared schema or redeploy Witan as part of this task.
3. Create the RackPy project only if no matching active project exists; use repo https://github.com/feoh/RackPy and preserve owner decisions from README.md.
4. Start a spec-phase workflow session using the actual PI_SESSION_ID; attach epic and all children with task_update(project_slug=...). Preserve dependency/parent edges.
5. Update the manifest with real returned handles and a repair note; read project/task records back and verify repository normalization and task counts.

### Validation

- workflow_project_get and task_list(project_slug=...) show the epic and every child exactly once.
- Compare blocked_by and parent before/after reassociation; no change allowed.
- Repository JSON and DAG checks pass.

### Acceptance criteria

- A real project slug is persisted and verified, not invented.
- If service remains broken, leave this task open with an actionable checkpoint; all other independent planning work remains valid.

**Notes:** This is metadata recovery, not authorization to fix Witan infrastructure.

## RP-02 — Pin the SDK baseline and define the complete public-surface contract

**Priority:** p1 · **Depends on:** none

### Deliverables

- `upstream/versions.json`
- `docs/compatibility.md`
- `docs/coverage.md`
- `tests/fixtures/sdk-provenance.json`

### Implementation steps

1. Recheck Rack release/tag and the planned v2.6.6 commit. Record whether to retain the research baseline; do not silently target a mutable v2 branch.
2. Acquire SDK headers/toolchain dependencies with immutable URL or recorded SHA256 outside git. Record OS, architecture, compiler flags, CPython candidates and SDK build/ABI constraints.
3. Read rack.hpp and enumerate its public include closure, PRIVATE annotations, public deprecated APIs, macros, template declarations and platform branches.
4. Compare include closure with supported documentation; distinguish forward-declared internals and transitive external types needed for public operations.
5. Write an explicit completeness policy with overload, raw-pointer, macro, template, callback and platform rules. Identify unsupported semantics as blockers, not exclusions of convenience.
6. Define initial Linux x86_64 validation target and candidate macOS/Windows architectures from actual SDK availability; label every untested target.

### Validation

- SDK archive checksums reproduce on a clean download.
- A plugin translation unit including only rack.hpp compiles with the selected toolchain.
- Review the surface-family checklist in docs/scope.md against the pinned include closure.

### Acceptance criteria

- All baseline inputs are immutable or content-hashed.
- No count of full coverage is claimed before extraction.
- Owner review required for any reduction of the complete-public-API goal.

## RP-03 — Audit licenses, package identity and every official sample source

**Priority:** p1 · **Depends on:** RP-02

### Deliverables

- `NOTICE`
- `docs/licensing.md`
- `upstream/examples.json`
- `docs/decisions/001-licensing-and-identity.md`

### Implementation steps

1. Read pinned Rack, SDK, Python, candidate binding library and transitive dependency terms. Keep source/binary/artwork obligations separate; GPL-3.0-or-later is the owner decision, not permission to relicense third-party assets.
2. Capture official PluginDevelopmentTutorial and PluginGuide text with URL, retrieval timestamp, content hash and evidence of adaptation permission. If terms are unclear, prepare an owner-approved inquiry and block derivative ports, not original bindings.
3. Enumerate every code block and generated helper template into stable sample IDs with section, feature, license/provenance, supporting-fragment/duplicate/shell/port classification and intended port task.
4. Check RackPy GitHub/PyPI/distribution-name availability and remaining naming conflicts without registering or publishing packages. Do not use VCV logos or clone panels.
5. Document source-offer/notices and bundled CPython/native-library obligations for artifacts. Use original sample layouts/assets; inspect any optional Fundamental source adaptation separately.

### Validation

- Every required guide/tutorial block has an inventory disposition and no missing source/license field.
- Asset ledger contains no copied restricted artwork or fonts.
- Proposed artifact contents are checked against redistribution permissions.

### Acceptance criteria

- Implementation may proceed under documented terms; any unresolved obligations have explicit gates.
- No manual-derived sample is adapted until provenance/permission is established.
- External inquiries and namespace registration require owner approval.

## RP-04 — Prove CPython embedding and Rack-compatible binding technology

**Priority:** p1 · **Depends on:** RP-02, RP-03

### Deliverables

- `docs/decisions/002-embedding-and-bindings.md`
- `spikes/embedding/`
- `tests/integration/test_embedding_smoke.py`

### Implementation steps

1. Build a minimal real Rack-loadable plugin including only rack.hpp; initialize CPython via explicit configuration, not ambient Python discovery.
2. Compare pybind11 and nanobind on ownership transfer, Python subclass trampolines, typed buffers, embedding, toolchain/standard constraints and binary size. Choose one based on measured feasibility, not preference.
3. Expose one value, one Rack-owned object, one safe UI callback and one native process kernel. Distinguish in-host binding import from standalone harness symbol resolution.
4. Exercise two Rack contexts, another preexisting interpreter/runtime and duplicate host loading; specify supported versus refused combinations.
5. Document SDK loader/export/link flags and runtime search/rpath/DLL behavior on Linux, plus concrete macOS/Windows risk probes.
6. Record hard blockers and alternatives. Keep spike code isolated from production; do not bootstrap the full API on an unproven lifetime model.

### Validation

- Load plugin in actual Rack with a temporary user profile, construct/delete a module and exit without crash.
- Missing/wrong CPython runtime yields actionable diagnostics without C++ exceptions crossing init.
- Run value/ownership/callback smoke tests and record binary dependencies.

### Acceptance criteria

- A real host-load trace and reproducible commands exist.
- Binding library/Python ABI candidates and supported interpreter ownership are chosen.
- If no legal/reliable embedding exists, stop and request a revised architecture.

## RP-05 — Build an exhaustive AST and preprocessor inventory of the public API

**Priority:** p1 · **Depends on:** RP-02

### Deliverables

- `bin/inventory_sdk.py`
- `upstream/api-surface.json`
- `tests/inventory/`
- `docs/reference/surface-schema.md`

### Implementation steps

1. Create a uv-managed cyclopts inventory utility using Clang AST/libclang plus preprocessor records. The entry translation unit includes only rack.hpp.
2. Use stable IDs for canonical signatures, overloads, methods, members, enums, callbacks, globals, aliases, templates and relevant macros; retain source locations and platform conditions.
3. Classify PRIVATE/internal fields with explicit evidence while retaining public deprecated declarations. Preserve unknowns for review.
4. Account for external types reachable in signatures and public behavior inherited from transitive types; do not double-count namespace imports or aliases.
5. Run extraction for all selected target defines or native target SDKs. Record gaps when cross extraction lacks system dependencies.
6. Emit deterministic sorted data and a human family summary. Add fixtures for overloads, PRIVATE, conditional declarations, template defaults, macros and renamed paths.

### Validation

- Two identical runs produce byte-identical normalized inventory.
- Mutation fixtures add an overload/macro/platform branch and are detected.
- Spot-check Module, Widget, DSP templates, SIMD and context against actual headers.

### Acceptance criteria

- Every supported public declaration has a record or explicit extraction-error blocker.
- An extraction failure cannot create an apparently complete empty report.
- No hand-written header checklist substitutes for the inventory.

## RP-06 — Prove thread affinity, lifetime and callback semantics before production code

**Priority:** p1 · **Depends on:** RP-04

### Deliverables

- `docs/decisions/003-lifetimes-and-threads.md`
- `spikes/lifecycle/`
- `tests/integration/test_lifecycle_probe.py`

### Implementation steps

1. Trace actual Rack call sites for module construction/deletion, events, JSON, widget callbacks, engine operations and plugin destroy; record thread and synchronization evidence against the pinned source.
2. Test GIL/engine-lock inversion and deferred destruction scenarios with UI callbacks, sample-rate changes, save/load and concurrent module removal.
3. Specify owner/borrower/transfer policies, identity registry, context generation tokens, view invalidation, strong Python callback references and callback shutdown ordering.
4. Classify each virtual method as UI-safe, native-only production path with an explicit Python semantic laboratory path, or unresolved. Do not assume mutually exclusive module events are UI callbacks.
5. Design synchronous JSON/state adapters and bounded control/snapshot queues without waiting for Python on an audio thread.
6. Prove no Python refcount/destructor work is required by native process/deletion. Test null module browser previews and window recreation.

### Validation

- Stress add/delete/save/close loops in a real host and record thread IDs off the audio path.
- Intentional lock inversion is detected/refused rather than hanging indefinitely; use time-bounded subprocess tests.
- Callback-after-delete and cross-context access raise defined failures.

### Acceptance criteria

- A reviewed thread/lifetime matrix covers every callback family.
- Unresolved synchronous semantics block the corresponding implementation tasks.
- Tests demonstrate teardown policy rather than relying on exit-time OS cleanup.

## RP-07 — Measure feasibility and freeze performance and real-time budgets

**Priority:** p1 · **Depends on:** RP-06

### Deliverables

- `benchmarks/spike/`
- `benchmarks/baseline.json`
- `docs/decisions/004-performance-budgets.md`

### Implementation steps

1. Implement equivalent native-only and bound-host sine/gain/polyphonic benchmark fixtures using the same DSP and compiler flags.
2. Measure native boundary costs, Python laboratory callbacks separately, startup, copies and memory. Preserve Rack per-sample scheduling; no hidden engine-wide batching.
3. Instrument production-path allocations, Python/GIL/refcount events, locks and diagnostic writes.
4. Collect repeated A/B measurements on controlled hardware at selected sample rates/channel counts; keep raw results and complete environment metadata.
5. Assess docs/performance.md candidate budgets, absolute trivial-kernel cost and stress-test duration. Record achievable budgets and request owner approval for changes.
6. Make a go/no-go recommendation before scaffold/binding work fans out; identify kernels/queues that need design changes.

### Validation

- Re-run measurements from a clean build and reproduce signal equivalence.
- Forced slow Python UI does not stall native processing in the proposed design.
- Instrumentation detects intentionally injected forbidden audio-path calls.

### Acceptance criteria

- Budgets are numeric, workload-specific and justified by raw measurements.
- GIL release is not presented as evidence of real-time safety.
- Failure to achieve feasible native-path behavior triggers replanning.

## RP-08 — Scaffold reproducible builds, developer checks and CI

**Priority:** p2 · **Depends on:** RP-03, RP-04, RP-05, RP-06, RP-07

### Deliverables

- `pyproject.toml`
- `uv.lock`
- `CMakeLists.txt`
- `src/rackpy/`
- `native/`
- `tests/`
- `bin/`
- `.github/workflows/`

### Implementation steps

1. Move only validated spike patterns into a minimal production host/binding skeleton; document why spike files are retained or deleted.
2. Configure uv, chosen binding library and CMake/scikit-build-core or the alternative selected by RP-04; prove compatibility with Rack's actual SDK flags and host packaging.
3. Pin Python/tool versions and checksummed SDK acquisition. Build dependencies outside source control; never vendor SDK/Rack Pro binaries accidentally.
4. Add strict Ruff/mypy or equivalent checks for Python, native warnings, unit/integration markers and prek hooks. CLI utilities use cyclopts.
5. Create CI jobs for planning data, Python/static/native unit checks, SDK compilation and a Linux host smoke test with explicit artifact retention.
6. Define local commands and clean-room setup in CONTRIBUTING.md; include no-audio-device and GUI test requirements.

### Validation

- uv sync --frozen works in a clean worktree.
- Hooks, typing, native build and minimal tests pass locally and in CI.
- Built skeleton loads into actual Rack; wheel-only import success is not accepted.

### Acceptance criteria

- Reproducible build/test commands and CI evidence exist.
- No placeholder runtime is documented as complete.
- SDK absence produces a clear setup error, not a successful skip of required checks.

## RP-09 — Create the documentation site skeleton and executable-snippet pipeline

**Priority:** p2 · **Depends on:** RP-08

### Deliverables

- `docs/`
- `documentation build configuration`
- `tests/docs/`
- `CI docs job`

### Implementation steps

1. Evaluate Sphinx versus MkDocs with the chosen native binding/stub strategy; pin one and record a short decision.
2. Implement docs/documentation.md navigation with a clearly marked planning/development status, accessibility defaults and version metadata.
3. Generate reference scaffolding from manifest/stubs without loading a GUI or fabricating a runtime mock.
4. Add warning-as-error builds, local-link checks, bounded external-link checks and extractable executable snippets.
5. Define tutorial source-of-truth conventions so source files and displayed snippets cannot drift; use explicit conceptual-code labels.

### Validation

- Site builds with zero warnings and unresolved internal references.
- A deliberately broken snippet/link fails the right CI job.
- Generated reference scaffolding is labeled incomplete until the mapped implementation exists.

### Acceptance criteria

- Readers can find installation status, architecture, examples and reference gaps.
- No instructions imply a released runtime before packaging tasks pass.

## RP-10 — Implement the interpreter owner, context handles and safe teardown

**Priority:** p2 · **Depends on:** RP-08

### Deliverables

- `native/runtime/`
- `src/rackpy/errors.py`
- `tests/runtime/`

### Implementation steps

1. Implement CPython configuration, runtime discovery and single-owner negotiation exactly as selected in RP-04; isolate imports from cwd/ambient PYTHONPATH by default.
2. Add per-context generation tokens, identity/ownership registry and explicit owner/borrower/transfer wrappers. Preserve object identity for repeated views.
3. Implement UI-thread assertions, scoped context restoration, bounded diagnostics and deterministic shutdown ordering.
4. Retain Python callback references until native detachment; move destruction/refcount transitions off audio threads. Refuse unsafe finalization while callbacks survive.
5. Translate Python/native errors to public exception types and prevent propagation through Rack C ABI boundaries.
6. Document untrusted-code limitations, runtime collision behavior and recovery from import/initialization failure.

### Validation

- Repeated host load/unload and 1,000 create/delete cycles do not leak or crash.
- Cross-context/stale handles, double transfer and wrong-thread operations fail predictably.
- Missing runtime, already initialized incompatible runtime, exception during init and teardown are subprocess-tested.

### Acceptance criteria

- No dangling handles or Python destructor work on production audio paths.
- Actual-host smoke test proves supported interpreter lifecycle.
- Every public error type has documentation and tests.

## RP-11 — Implement binding manifest, generation and coverage enforcement

**Priority:** p2 · **Depends on:** RP-05, RP-08

### Deliverables

- `bindings/manifest.json`
- `bin/generate_bindings.py`
- `bin/check_coverage.py`
- `tests/coverage/`

### Implementation steps

1. Define the per-symbol schema from docs/coverage.md; seed all extracted symbols as unreviewed/planned, never automatically verified.
2. Generate reviewable registration scaffolds, stubs and C++→Python mapping data using deterministic ordering; keep hand-written adapters separate.
3. Support methods/overloads/properties/enums/templates/macros/callback policies and platform variants with explicit implementation/test/doc links.
4. Make coverage checks introspect the built package and validate referenced tests/docs; no counting aliases or NotImplementedError stubs as complete.
5. Add strict stale-source and upstream-diff detection with an explicit reviewed internal-exclusion allowlist.
6. Create per-family/platform reports showing unknown and unrepresentable entries prominently; document how later family tasks update mappings.

### Validation

- Fixtures detect missing overloads, nonexistent doc/test paths, stale signatures and bogus exclusions.
- Repeated generation is clean; manually edited generated output fails regeneration checks.
- Empty inventories, parser failures and missing runtime artifacts fail rather than report 100%.

### Acceptance criteria

- Coverage denominator is immutable relative to the pinned extracted surface.
- Partial implementations can be reported but cannot satisfy full-release checks.
- Generator utilities use cyclopts and strict typing.

## RP-12 — Bind common values, scalar utilities, enums and generic boundary types

**Priority:** p2 · **Depends on:** RP-10, RP-11

### Deliverables

- `native/bindings/values/`
- `src/rackpy/math.py`
- `src/rackpy/types.py`
- `tests/values/`

### Implementation steps

1. Select this family's exact manifest IDs: math/Vec/Rect, colors, quantity bases, common enums/constants and value helpers; assign remaining utility IDs to RP-30 explicitly.
2. Implement checked conversions, arithmetic/operators, repr/equality and nullability while preserving upstream numeric semantics.
3. Distinguish copied immutable values from live mutable views; do not turn reference-returning objects into misleading independent copies.
4. Design shared JSON/buffer/capsule policy types needed by later families without exposing raw integer pointers.
5. Handle relevant macros/constants as documented Python values or operations; preserve deprecation metadata.
6. Update raw/facade mappings, type hints, prose and tests for each implemented symbol.

### Validation

- Differential tests compare numeric results/edge cases with compiled C++ fixtures.
- Wrong types, extreme floats, nonfinite values, out-of-range enums and stale views have defined outcomes.
- Coverage report for this family contains no unexplained omissions.

### Acceptance criteria

- All family manifest IDs are verified or explicitly remain open blockers.
- No duplicated math engine subtly diverges from Rack.
- Document value-copy versus live-view behavior.

## RP-13 — Bind ports, parameters, lights and polyphonic buffer access

**Priority:** p2 · **Depends on:** RP-12

### Deliverables

- `native/bindings/engine/components.cpp`
- `src/rackpy/engine/components.py`
- `tests/engine/test_components.py`

### Implementation steps

1. Implement exact Param/Input/Output/Port/Light/metadata semantics, including channel count, normaling, connected state, voltage helpers, smoothing and brightness methods.
2. Provide checked indexed/named collections and useful properties; validate all IDs before Rack assert-only accessors.
3. Map read/write/SIMD port methods without silent scalar-channel or disconnected-port behavior changes.
4. Implement owned snapshots and any zero-copy views with explicit lifetime, thread and resize rules. Reject unsafe views rather than copy secretly.
5. Inventory the actual pinned polyphony limit instead of assuming a hard-coded maximum in every layer.
6. Add raw/facade reference, units and examples; mark operations requiring engine synchronization.

### Validation

- Native differential tests cover 0/1/max channels, disconnected inputs, normaling and polyphonic sums.
- Negative/oversized indices, removed modules and retained views cannot access freed memory.
- Concurrent UI/engine access follows the RP-06 policy and is stress-tested.

### Acceptance criteria

- Every component overload/property has a mapping and behavioral test.
- Buffer ownership and copy behavior are documented and measurable.
- No audio path creates Python views or refcounts.

## RP-14 — Bind module configuration, lifecycle, bypass and native event adapters

**Priority:** p2 · **Depends on:** RP-06, RP-13

### Deliverables

- `native/module/`
- `native/bindings/engine/module.cpp`
- `src/rackpy/engine/module.py`
- `tests/engine/test_module.py`

### Implementation steps

1. Implement Module construction/configuration, parameter display scaling, switch/button metadata, bypass routes and component lookup with constructor-phase restrictions.
2. Implement identity/model IDs and native production adapters for lifecycle/event notifications using the thread matrix.
3. Preserve superclass behavior for reset/randomize/sample-rate handling; don't copy the manual's suspect ResetEvent randomize signature.
4. Define immutable event snapshots where asynchronous observation is safe, but retain synchronous semantics in native handlers and the laboratory path.
5. Handle browser-preview module=None, invalid configuration, repeated configuration and partial-constructor exceptions.
6. Update every public Module symbol's mapping, including deferred JSON, process/callback and expander entries assigned to their later tasks.

### Validation

- Construct/configure/delete modules in actual Rack; reset/randomize/bypass/sample-rate changes match native fixtures.
- Parameter scales, labels, defaults and bypass routing survive save/reload.
- Wrong-phase calls and invalid IDs raise errors before Rack assertions.

### Acceptance criteria

- Production engine callbacks never invoke Python unless the non-real-time laboratory is explicitly enabled.
- Each Module symbol has a responsible implementation task.
- Default superclass behaviors remain intact.

## RP-15 — Implement JSON state, patch storage and plugin settings safely

**Priority:** p2 · **Depends on:** RP-14

### Deliverables

- `native/state/`
- `src/rackpy/state.py`
- `tests/state/`
- `docs/how-to/state.md`

### Implementation steps

1. Implement Jansson ownership/refcount adapters for dict/list/scalars/null with explicit integer range, nesting, Unicode, key and nonfinite policies.
2. Bind toJson/fromJson, params and custom-data callbacks with the correct default/super behavior; distinguish missing data from null data.
3. Implement native/snapshot state needed for synchronous save/load without blocking the production audio path waiting on Python.
4. Expose patch storage directory methods with add/save lifecycle restrictions and pathlib facade; never auto-delete undo-restorable storage.
5. Wire plugin-wide settings callbacks after registration support lands; retain this task open until integration is proven or split an explicit dependent task.
6. Version example state schemas and handle malformed/unknown/future fields without executing code or unpickling.

### Validation

- Round-trip state through actual .vcv save/reopen, autosave and undo deletion.
- Malformed JSON, deep nesting, large ints, bad UTF-8, absent files and filesystem failures are tested.
- Native leak checks verify borrowed/new Jansson references and error cleanup.

### Acceptance criteria

- No hidden synchronous Python callback can stall production processing.
- State behavior is documented with threading and persistence guarantees.
- All JSON/storage symbols have behavioral mappings, not placeholder serializers.

**Notes:** Plugin-wide callbacks can use the runtime host skeleton; RP-16 must exercise registration integration without introducing a dependency cycle.

## RP-16 — Implement plugin/model registration, factories and trusted script loading

**Priority:** p2 · **Depends on:** RP-14, RP-15

### Deliverables

- `native/plugin/`
- `src/rackpy/plugin.py`
- `src/rackpy/assets.py`
- `tests/plugin/`

### Implementation steps

1. Implement init/destroy/settings C ABI boundaries and Plugin/Model public API with appropriate host ownership.
2. Provide Python model/module/widget registration with validation, deterministic slugs and cleanup on partial registration failure.
3. Map createModel and relevant helper templates to faithful callable/factory semantics; record generic constraints and unsupported specializations as open coverage blockers.
4. Load only explicitly installed/trusted scripts from configured locations; patches reference registered models, never arbitrary code paths or network URLs.
5. Expose asset lookup without arbitrary host ownership transfer; implement null-module preview factories.
6. Define safe plugin/module version behavior and diagnostics for missing scripts, dependencies, slug conflicts and imported exceptions.

### Validation

- Register two models; instantiate in browser and rack; remove/reopen/exit in actual host.
- Duplicate slugs, bad constructors, missing assets and exceptions leave no partial registry corruption.
- Plugin settings and JSON integration from RP-15 round-trip.

### Acceptance criteria

- Model factories and plugin hooks have tested mappings.
- No auto-execution of patch-supplied code.
- All C ABI exception paths are contained and surfaced actionably.

## RP-17 — Implement production-native DSP state and extensible kernel interfaces

**Priority:** p2 · **Depends on:** RP-07, RP-14

### Deliverables

- `native/dsp/runtime/`
- `src/rackpy/dsp/kernel.py`
- `tests/dsp/test_runtime.py`

### Implementation steps

1. Define a native kernel ABI and state ownership suitable for the exact per-sample Rack process contract.
2. Connect ports/params/lights directly to native kernel state; allocate and compile outside the audio thread.
3. Implement bounded control/snapshot channels and off-thread reclamation of replaced state with an explicit overflow policy.
4. Provide a documented C++ extension mechanism for custom kernels, including version/ABI checks, error containment and lifetime rules.
5. Implement native bypass, reset, rate changes and deterministic silence/bypass on failure without Python callbacks or audio-thread logging.
6. Instrument forbidden GIL/Python/refcount/allocation paths for tests.

### Validation

- Native gain and oscillator kernels match direct C++ sample-for-sample within documented float tolerances.
- Queue saturation, state replacement and teardown under engine load preserve safety.
- Injected forbidden operations are detected; healthy process paths have zero forbidden events.

### Acceptance criteria

- Python can configure and select kernels without executing per sample.
- Custom native kernels are supported without editing the host internals.
- Lifetime and failure behavior meet frozen RP-07 budgets.

## RP-18 — Bind DSP common, digital, filter and mathematical algorithms

**Priority:** p2 · **Depends on:** RP-17, RP-11

### Deliverables

- `native/bindings/dsp/basic.cpp`
- `src/rackpy/dsp/`
- `tests/dsp/test_basic.py`

### Implementation steps

1. Enumerate exact IDs in dsp/common, approx, digital, filter, convert, window, ode, midi and vumeter; verify actual public declarations rather than assuming header names cover everything.
2. Expose triggers, timers, filters and helpers as stateful native Python objects with clear reset/sample-rate semantics.
3. Map templated numeric/vector variants using reviewed generic factories or explicit type-specialization registration; do not claim a scalar-only subset is complete.
4. Offer bulk buffer operations where meaningful, keeping native implementations for production graph nodes.
5. Handle mutable state, denormal/nonfinite inputs and conversion policies faithfully; document any deliberate safety exceptions.
6. Update manifest, stubs, explanation and small differential examples for every family ID.

### Validation

- Compare impulse/step/trigger streams and randomized inputs against C++ fixtures.
- Test reset, rate changes, parameter extremes and state retention over many calls.
- Python per-call versus bulk timings and copy counts are reported, not assumed.

### Acceptance criteria

- All assigned public symbols and overloads are verified or remain explicit blockers.
- DSP results and state transitions match the pinned SDK.
- No Python implementation substitutes for native real-time kernels.

## RP-19 — Bind DSP FFT, resampling, FIR, MinBLEP and ring buffers

**Priority:** p2 · **Depends on:** RP-18

### Deliverables

- `native/bindings/dsp/advanced.cpp`
- `tests/dsp/test_advanced.py`
- `docs/reference/dsp.md`

### Implementation steps

1. Inventory dsp/fft, resampler, fir, minblep and ringbuffer IDs including template size/type/order parameters and dependency types.
2. Implement plan/state ownership and memory alignment, buffer shapes/strides/dtypes and clear copy-versus-view policies.
3. Provide finite-size/type factories and a reviewed extension mechanism where needed for semantic template coverage; unresolved generality remains a release blocker.
4. Map constructor/reset/configuration/process methods and overloaded buffer operations; define in-place/out-of-place behavior.
5. Ensure expensive initialization and destruction never occur on production audio paths; add graph-compatible native adapters as required.
6. Document normalization, latency, channel order, capacity and underrun/overflow semantics.

### Validation

- FFT forward/inverse and known-bin tests verify normalization; resampling verifies signal/latency against native fixtures.
- Ring buffer wrap/full/empty and FIR/MinBLEP boundary tests run under sanitizers.
- Wrong dtype/shape/alignment, retained view and extreme sizes fail safely.

### Acceptance criteria

- Every assigned declaration has a reachable Python mapping or an explicit open blocker.
- No silent copies or implicit plan creation on process paths.
- Raw data tests compare the actual SDK implementation.

## RP-20 — Expose SIMD vectors and functions without hiding template gaps

**Priority:** p2 · **Depends on:** RP-12, RP-17

### Deliverables

- `native/bindings/simd/`
- `src/rackpy/simd.py`
- `tests/simd/`

### Implementation steps

1. Enumerate public Vector/functions declarations, aliases, operators, masks, loads/stores and scalar/vector overloads.
2. Expose typed native vectors with lane access and buffer conversion; validate width/alignment and preserve mask/comparison semantics.
3. Map math functions and constants; distinguish scalar conversion from broadcasting and test edge behavior.
4. Integrate SIMD-aware port/DSP methods and graph kernels without requiring Python allocation per sample.
5. Record platform implementation differences, required CPU features and template extension rules in the manifest.
6. Do not broaden low-level SIMD header internals into public API just because the parser sees them.

### Validation

- Randomized lane-by-lane comparisons against C++ fixtures include NaN/infinity/masks where supported.
- Misaligned/short loads, bad lane indices and wrong vector types fail safely.
- Each advertised target executes a native SIMD smoke test, not only cross-compiles.

### Acceptance criteria

- All public SIMD semantics have documented Python equivalents.
- Template restrictions remain visible in coverage.
- No architecture is advertised from x86-only testing.

## RP-21 — Compile Python-authored DSP graphs into sample-accurate native plans

**Priority:** p2 · **Depends on:** RP-18, RP-19, RP-20

### Deliverables

- `native/dsp/graph/`
- `src/rackpy/dsp/graph.py`
- `tests/dsp/test_graph.py`

### Implementation steps

1. Define typed graph nodes and connection validation for native oscillator/arithmetic/gain, triggers, light control and polyphonic processing.
2. Compile immutable execution plans outside audio processing; preallocate all state and bind Rack port/param/light storage safely.
3. Preserve per-sample execution order, sample-rate changes, intra-module feedback and expander boundaries. Reject unsupported cycles with actionable diagnostics rather than adding latency silently.
4. Add native-kernel extension nodes from RP-17; document that arbitrary Python functions are not magically compiled.
5. Implement bounded plan publication and off-thread reclamation under concurrent edits/deletion.
6. Provide a small Python offline reference for tests; clearly separate it from the production plan.

### Validation

- Sine/blink, VCA and polyphonic graphs match equivalent native modules.
- Feedback, control changes mid-run, plan replacement and bad cycles are tested.
- Instrumented 16-channel runs meet RP-07 safety invariants and initial cost budgets.

### Acceptance criteria

- First-class sample ports can be expressed without Python audio callbacks.
- No cross-module batching changes Rack scheduling.
- Unknown graph behavior is rejected, not silently approximated.

## RP-22 — Implement explicit non-real-time Python callback coverage laboratory

**Priority:** p2 · **Depends on:** RP-07, RP-10, RP-14

### Deliverables

- `native/laboratory/`
- `src/rackpy/laboratory.py`
- `tests/laboratory/`
- `docs/explanation/callbacks.md`

### Implementation steps

1. Prove a public-API-only offline/non-real-time host can drive Module process/processBypass and synchronous callbacks, or record the infeasibility and request an explicit scope decision.
2. Implement Python subclass trampolines with intentional GIL acquisition only in the laboratory, preserving signature, return, superclass and exception semantics.
3. Require explicit trusted-user enablement; production host paths refuse accidental Python audio callbacks with a clear explanation.
4. Expose synchronous event/JSON/custom override semantics that asynchronous observation cannot preserve. Inventory every public virtual method and map its legitimate execution domain.
5. Bound callback lifetime to context, track reentrancy, restore thread-local context, and contain Python exceptions.
6. Document measured costs and why a Python callback benchmark is not production real-time performance.

### Validation

- Override process, bypass, reset/randomize/rate and JSON methods; compare callback order/results with native fixtures.
- Exception, recursive call, stale object and shutdown races are subprocess-tested.
- Attempting to register a Python process callback in normal mode is rejected.

### Acceptance criteria

- Each public callback has a real semantic Python execution path or remains a full-release blocker.
- Laboratory restrictions are prominent in reference and tutorials.
- No unsafe path is silently enabled by loading a patch.

## RP-23 — Implement safe expander access and one-sample message exchange

**Priority:** p2 · **Depends on:** RP-14, RP-21

### Deliverables

- `native/engine/expanders.cpp`
- `src/rackpy/engine/expanders.py`
- `tests/engine/test_expanders.py`

### Implementation steps

1. Map Expander fields and requestMessageFlip through typed native message schemas instead of integer-address casts.
2. Preserve left/right, adjacent-module identity/model checks, producer/consumer access modes and exact double-buffer ownership.
3. Allocate two buffers off the audio thread; native kernels produce/consume messages, with Python receiving safe snapshots or laboratory views.
4. Handle model mismatch, removal, rearrangement and reconnection by invalidating handles and clearing outputs as specified.
5. Document interoperability with custom native schemas and the extension contract for raw void* semantics; unresolved arbitrary-pointer coverage remains visible.
6. Connect expander callbacks to RP-14/RP-22 paths without introducing Python into production processing.

### Validation

- Native pair test proves exactly one-engine-frame latency across sample rates and engine thread counts.
- Hot-remove/reorder/mismatch and repeated buffer flips run under sanitizers.
- Invalid schema, foreign plugin message and stale views cannot reinterpret unsafe memory.

### Acceptance criteria

- Expander API is mapped with explicit safety/extension semantics.
- No direct cross-thread module-state reads masquerade as messages.
- One-sample behavior is measured in an actual host integration fixture.

## RP-24 — Bind engine operations, cables, parameter handles and context-dependent edits

**Priority:** p2 · **Depends on:** RP-13, RP-14, RP-16

### Deliverables

- `native/bindings/engine/engine.cpp`
- `src/rackpy/engine/host.py`
- `tests/engine/test_host.py`

### Implementation steps

1. Enumerate Engine/Cable/ParamHandle public APIs, lifecycle ownership and locking/thread rules from actual call sites.
2. Implement approved add/remove/update/query operations with native synchronization and stable IDs; avoid holding the GIL while waiting on engine locks.
3. Map cable endpoint/index validation, connected state, parameter automation handles and object identity across edits/undo.
4. Expose synchronous versus queued operations honestly; preserve semantics in raw/laboratory paths and document completion/error behavior in the facade.
5. Handle context-bound access and any publicly available history/patch-facing operations without including unsupported internal headers.
6. Catch all exceptions and rollback partial edits so failed script commands do not corrupt a patch.

### Validation

- Create/connect/disconnect/delete a small graph in actual Rack and compare with native fixtures.
- Invalid endpoints, duplicate objects, undo/redo, concurrent engine activity and stale handles are covered.
- Time-bounded tests detect engine/GIL deadlock and lock-order regressions.

### Acceptance criteria

- Every assigned engine symbol has thread/ownership documentation.
- No PRIVATE engine functions or guessed patch/history internals are used.
- Failed operations leave the engine coherent.

## RP-25 — Bind widget trees, transforms and scoped widget events

**Priority:** p2 · **Depends on:** RP-10, RP-11, RP-12

### Deliverables

- `native/bindings/widget/`
- `src/rackpy/widget.py`
- `tests/widget/`

### Implementation steps

1. Inventory Widget, Transparent/Opaque/OpenGl/Transform/Zoom/Svg/Framebuffer public bases and lifecycle members.
2. Implement child ownership transfer, removal, traversal, layout bounds, transform values and reference invalidation.
3. Bind event classes and propagation/consumption semantics with callback-scoped objects; do not persist borrowed event references.
4. Implement Python UI-thread subclass trampolines and base-method calls with error containment.
5. Support null module/browser contexts, window recreation and teardown without retained graphics resources.
6. Separate drawing/resource implementations assigned to RP-27 from shared widget ownership primitives.

### Validation

- Native/Python widget trees agree on traversal, bounds and event propagation.
- Child reparent/double add/remove, callback exception and callback-held stale refs fail safely.
- Actual host UI smoke covers module browser, module removal and close.

### Acceptance criteria

- All public widget lifecycle semantics are mapped, not only a rectangle wrapper.
- Transferred children have one owner and predictable Python identity.
- UI-thread restrictions are enforced.

## RP-26 — Bind UI controls, menu helpers and keyboard/mouse interactions

**Priority:** p2 · **Depends on:** RP-25

### Deliverables

- `native/bindings/ui/`
- `src/rackpy/ui.py`
- `tests/ui/`

### Implementation steps

1. Map all reachable ui headers: menus/items/overlays, labels/tooltips, lists/layouts, buttons/options/radio/choice, sliders/scrollbars/fields/progress and scroll containers.
2. Implement Python actions/submenu builders and typed bool/index state adapters without unsafe pointers to temporary Python objects.
3. Expose keyboard/mouse/drag/focus/text event types and propagation with correct callback lifetime.
4. Retain callback references until UI detachment and detach before interpreter shutdown; handle menu closures after module deletion.
5. Map relevant create* helper templates/factories with documented specialization semantics.
6. Document preview/null-module behavior and integrate exceptions into UI diagnostics without crashing Rack.

### Validation

- Exercise actions, toggles, choices, text Unicode, scrolling, event consumption and focus in actual host UI.
- Close menus after deleting their module; no stale callback access.
- Wrong-thread UI calls and invalid callback return types are tested.

### Acceptance criteria

- Every public control/event/helper ID has a mapping and test.
- Menu pointer helpers have safe state ownership.
- No UI operation writes live engine storage without the approved channel.

## RP-27 — Bind window/resources, NanoVG drawing and framebuffer behavior

**Priority:** p2 · **Depends on:** RP-25

### Deliverables

- `native/bindings/window/`
- `native/bindings/drawing/`
- `src/rackpy/drawing.py`
- `tests/rendering/`

### Implementation steps

1. Enumerate Window/Svg/Font/Image/shared resource APIs and external NanoVG types necessary to use DrawArgs.
2. Implement scoped draw contexts, colors/paths/text/images/transforms and layer semantics sufficient for the complete public drawing contract, not only the sample rectangle.
3. Map OpenGl and framebuffer-related public behavior with explicit context ownership; use safe handles rather than raw addresses.
4. Load/release resources on correct threads; do not retain Font instances across destroyed/recreated Rack windows.
5. Provide framebuffer invalidation/dirty behavior and light/dark settings integration with native resource lifetimes.
6. Use original test assets with clear licenses; record GPU/driver assumptions for visual tests.

### Validation

- Render guide shapes/text/self-lit layer and compare reviewed images with tolerances.
- Missing assets/fonts, window recreation, cached framebuffer invalidation and stale DrawArgs are tested.
- Real GL-context tests run separately from no-GPU tests; unavailable rendering is not reported as passed.

### Acceptance criteria

- Public drawing/resource semantics have executable Python access and lifetime docs.
- No retained borrowed draw context outlives its callback.
- Visual claims use actual captured output.

## RP-28 — Bind app widgets, component library and module helper factories

**Priority:** p2 · **Depends on:** RP-16, RP-26, RP-27

### Deliverables

- `native/bindings/app/`
- `native/bindings/components/`
- `src/rackpy/app.py`
- `tests/app/`

### Implementation steps

1. Inventory every reachable app/componentlibrary/helper declaration from rack.hpp and transitive public types; map ModuleWidget, RackWidget/Scene, param/port/light controls, SVG controls, displays and themed variants.
2. Implement module→widget association, param/port/light IDs, panel placement, SVG/resource loading and browser-preview None paths.
3. Translate createWidget/createParam/createInput/createOutput/createLight and centered/template variants into faithful checked Python factories.
4. Expose component classes without redistributing restricted upstream artwork; resolve installed resources or use original assets consistent with licensing policy.
5. Enforce tree ownership and cross-context/thread checks for scene/rack operations.
6. Update facade ergonomics and C++→Python maps; don't hide unbound app classes behind a single convenience module builder.

### Validation

- Build a complete original panel in actual Rack with working controls/ports/lights.
- Browser preview, dark-panel toggle, resource failure and remove/reopen are exercised.
- Factory overload/centering and ownership tests compare native layouts.

### Acceptance criteria

- All assigned app/helper/component IDs are reachable and documented.
- No restricted panel clone or mistaken artwork relicensing.
- Sample authors can create an instrument without manual native UI glue.

## RP-29 — Bind audio and MIDI device, port and driver interfaces

**Priority:** p2 · **Depends on:** RP-14, RP-17

### Deliverables

- `native/bindings/audio.cpp`
- `native/bindings/midi.cpp`
- `src/rackpy/audio.py`
- `src/rackpy/midi.py`
- `tests/io/`

### Implementation steps

1. Inventory all reachable audio/midi public types, device/driver enumeration, port configuration, messages, queues and callbacks.
2. Implement ownership and hotplug behavior with stable identities and bounded native queues.
3. Keep audio/device callbacks and production MIDI-to-DSP work native; provide Python snapshots/control operations and laboratory overrides for exact callback coverage.
4. Map sample/frame timestamps, channel layouts and message byte/range semantics without arbitrary silent normalization.
5. Build a deterministic fake-device/native loopback fixture; keep physical-device validation a separate opt-in gate.
6. Document permission/device errors, driver platform availability and lifecycle ordering during shutdown.

### Validation

- Fake audio/MIDI streams validate timestamps/order/overflow/device removal against C++ fixtures.
- Test 0 devices, permission failure, hotplug and active-stream module deletion.
- Each advertised OS runs native IO smoke tests; physical tests report actual hardware or remain unclaimed.

### Acceptance criteria

- All assigned public IO declarations have explicit mappings.
- No Python callback executes on production audio/device threads.
- Missing hardware cannot turn a required integration test into silent success.

## RP-30 — Bind public services: settings, assets, filesystem, network and synchronization

**Priority:** p2 · **Depends on:** RP-10, RP-12, RP-16

### Deliverables

- `native/bindings/services/`
- `src/rackpy/services/`
- `tests/services/`

### Implementation steps

1. Enumerate remaining public string/system/network/random/logger/mutex/settings/asset/common declarations and exposed transitive service types.
2. Map filesystem paths to pathlib facade and faithful raw strings; preserve errors, Unicode and cancellation behavior.
3. Expose HTTP/network and logger operations with safe GIL-release/callback ownership policy; never invoke blocking services in DSP.
4. Map settings globals with actual thread and persistence rules, not unsynchronized Python property writes.
5. Implement mutex/RAII constructs as explicit context-managed operations where faithful; document non-real-time and deadlock risks.
6. Handle variadic/macros/formatting functions with reviewed semantic adapters and test argument/format errors. Do not trim awkward declarations from coverage.

### Validation

- Use loopback HTTP fixtures and temporary directories; no public network required for deterministic tests.
- Test malformed paths/URLs, network timeout/cancel, Unicode, logger failure and synchronization misuse.
- Differential random/utility tests use controlled seeds or documented nondeterminism.

### Acceptance criteria

- All assigned utilities have complete behavior and blocking/thread restrictions.
- No credentials, ambient tokens or live personal paths in fixtures.
- Any remaining unsafe/unrepresentable service API stays a visible release blocker.

## RP-31 — Reconcile all transitive families and close binding-coverage gaps

**Priority:** p2 · **Depends on:** RP-18, RP-19, RP-20, RP-22, RP-23, RP-24, RP-28, RP-29, RP-30

### Deliverables

- `bindings/manifest.json`
- `docs/reference/coverage.md`
- `additional bounded Witan gap tasks if needed`

### Implementation steps

1. Run fresh inventory against all target platform SDKs and compare with every completed family task.
2. Audit public overloads/members, template generics, macro semantics, deprecated aliases, plugin callbacks, synchronization and transitive Quantity/history/patch/context types.
3. Identify genuine internals with source evidence; do not use exclusions merely because a declaration is difficult or unsafe.
4. For each missing cluster create a small Witan task with exact IDs, ownership/thread contract and tests; block this task on it. Do not bulk-mark generated stubs verified.
5. Check external type adapters make the public API usable, including drawing/JSON/void-pointer extension cases.
6. Get an independent code review through an authorized workflow or human reviewer, focused on scope cheats and inaccessible semantics.

### Validation

- Full manifest audit finds no orphan ID, nonexistent path or missing condition.
- Mutation tests deliberately remove overload/callback mappings and fail.
- Build/introspect all completed families on available native targets; unavailable ones stay unresolved.

### Acceptance criteria

- No known public family is omitted.
- All newly discovered gap tasks close before this reconciliation closes.
- Unrepresentable public semantics require owner scope review and keep full-release blocked.

## RP-32 — Polish and freeze the Pythonic facade and typing contract

**Priority:** p2 · **Depends on:** RP-31

### Deliverables

- `src/rackpy/`
- `type stubs and py.typed`
- `docs/reference/mapping.md`
- `tests/facade/`

### Implementation steps

1. Review facade coherently across all families: snake_case, enums, keyword-only options, properties, standard exceptions, iterables/context managers and useful reprs.
2. Keep expensive operations explicit and units obvious; avoid magical global APP access or implicit graph compilation in cheap-looking properties.
3. Verify stable C++→raw→facade discoverability, overload signatures, aliases and deprecation warnings.
4. Ship accurate generics/nullability/buffer typing; provide strict mypy/pyright sample checks without broad Any hiding missing contracts.
5. Benchmark convenience wrapper overhead and remove accidental per-element Python loops or copies on bulk operations.
6. Run a user walkthrough of representative module/UI/DSP/IO tasks; record confusing names and fix docs/tests together.

### Validation

- Strict typing checks all examples and facade tests.
- Signature/introspection and exception tests match reference docs.
- Each facade method delegates to tested native semantics with no duplicated divergent engine behavior.

### Acceptance criteria

- Pythonic design is consistent, documented and versioned.
- Raw access remains complete and discoverable.
- Facade simplification has not hidden upstream functionality.

## RP-33 — Port the official tutorial oscillator and generated plugin template

**Priority:** p2 · **Depends on:** RP-03, RP-21, RP-28, RP-32

### Deliverables

- `examples/oscillator/`
- `examples/template/`
- `tests/examples/test_oscillator.py`

### Implementation steps

1. Use RP-03's exact source IDs and approved adaptation terms; retain notices and record source content hash plus all intentional changes.
2. Port manifest/model registration, Python module configuration, original panel UI and sine/blink behavior to the actual facade. Do not display invented APIs.
3. Use a native graph/kernel for production process and a labeled pure-Python offline reference for teaching and parity.
4. Preserve 1V/octave, middle-C frequency, ±5 V output, 1 Hz blink and reset/rate behavior; document intentional numerical corrections.
5. Port the SDK-generated template workflow using supported tools and original assets; include a clean-room build/run guide.
6. Test actual Rack loading, browser previews, connecting ports, save/reopen and deletion; capture expected signal statistics.

### Validation

- Compare output against native C++ reference over fixed input streams at 44.1/48/96 kHz.
- Validate frequency, voltage amplitude, blink duty/rate and sample-continuity tolerances.
- Clean profile can build/load/run without undeclared dependencies; all sample Python passes strict typing.

### Acceptance criteria

- A working production port exists, not only an offline simulation.
- Every assigned source block has a tested disposition in upstream/examples.json.
- README includes exact commands, expected result, safety restrictions and provenance.

## RP-34 — Port guide configuration, lifecycle, state and context-menu examples

**Priority:** p2 · **Depends on:** RP-03, RP-15, RP-26, RP-33

### Deliverables

- `examples/configuration/`
- `examples/state-and-events/`
- `examples/menus/`
- `tests/examples/test_controls_state.py`

### Implementation steps

1. Port every RP-03 configuration block: input/output/light labels, display scales/units, switches/buttons, descriptions and bypass routes including custom bypass semantics.
2. Port reset/randomize/rate events using correct pinned signatures and production-native event adapters; provide laboratory equivalents where needed.
3. Port JSON/custom state, patch-storage load/save and plugin settings without UI/audio thread violations or arbitrary code execution.
4. Port action/bool/index/submenu menu examples with valid callback lifetimes and null-module browser handling.
5. Document native-versus-Python execution and any intentional correction of upstream omissions or mistakes.
6. Update the source-block inventory and executable docs references; keep sample assets original.

### Validation

- Compare tooltip/display/bypass/event behavior to native fixtures.
- Round-trip saved data, missing/future fields, patch assets and plugin settings in actual Rack.
- Menu callbacks after close/module deletion are safe; all examples build/load/type-check.

### Acceptance criteria

- Every assigned guide code block is ported or explicitly classified as supporting/non-runtime material.
- No asynchronous replacement silently changes synchronous callback semantics.
- Ports include provenance, complete runnable sources and expected outcomes.

## RP-35 — Port dark-panel, drawing, text, layer and framebuffer examples

**Priority:** p2 · **Depends on:** RP-03, RP-27, RP-28, RP-33

### Deliverables

- `examples/drawing/`
- `examples/dark-panels/`
- `tests/examples/test_drawing.py`

### Implementation steps

1. Port all captured drawing/text/self-illuminating/framebuffer/dark-panel source blocks using actual facade calls.
2. Create original light/dark SVG panels and original graphics/font assets or use properly licensed runtime resources; do not copy Fundamental visual designs.
3. Preserve geometry, layer ordering and dirty-cache semantics; document any intentional source correction such as mismatched color comments.
4. Handle missing fonts/resources and window recreation without retaining invalid Font or drawing handles.
5. Write a guided walkthrough with real screenshots, alt text and a text description of expected shapes/lighting.
6. Link each source block to its port/tests/docs and include browser-preview behavior.

### Validation

- Real-host image checks cover light/dark panel and room brightness/self-lit layers with reviewed tolerances.
- Framebuffer redraw/dirty tests prove caching invalidates correctly.
- Window close/reopen, missing font and widget/module deletion tests do not crash.

### Acceptance criteria

- Every assigned drawing source block has an executable port.
- No fabricated screenshot or restricted asset is committed.
- Manual GPU/platform gaps remain visible release gates.

## RP-36 — Port polyphony, SIMD, DSP and expander guide examples

**Priority:** p2 · **Depends on:** RP-03, RP-20, RP-21, RP-23, RP-29, RP-33

### Deliverables

- `examples/polyphony/`
- `examples/expanders/`
- `examples/dsp/`
- `tests/examples/test_dsp_expanders.py`

### Implementation steps

1. Port every assigned guide SIMD/polyphony/DSP block, preserving normaling, channel count and native vector behavior.
2. Port adjacent-module inspection and producer/consumer expander examples with model/type checks and typed message schemas.
3. Provide native production execution and labeled offline Python teaching equivalents without implying Python is real-time safe.
4. Demonstrate native DSP extension usage beyond the initial toy primitives and no-device IO fixtures where useful.
5. Use original panels/layouts, provenance files and clear sample-specific run/expected-output instructions.
6. Audit upstream/examples.json end-to-end for all tutorial/guide/template blocks; assign remaining blocks to explicit gap work rather than omitting them.

### Validation

- 16-channel signals and disconnected/partial-channel cases match native output.
- Expander tests prove one-frame latency under engine threading, rearrangement and removal.
- Every required sample builds, strict-type-checks, loads and survives save/reopen in actual Rack.

### Acceptance criteria

- All required official sample inventory entries are resolved with evidence.
- Fundamental stretch references are not mislabeled as official samples.
- Production sample DSP has zero Python audio-path events.

## RP-37 — Write and validate the complete human-readable user manual and reference

**Priority:** p2 · **Depends on:** RP-09, RP-32, RP-34, RP-35, RP-36

### Deliverables

- `docs/tutorials/`
- `docs/how-to/`
- `docs/explanation/`
- `docs/reference/`
- `docs/troubleshooting.md`

### Implementation steps

1. Implement docs/documentation.md's tutorial/how-to/explanation/reference structure with working installation status and supported-version tables.
2. Write a beginner-readable oscillator tutorial from the completed sample, including voltage units, native processing mental model and expected signals.
3. Cover ownership/threading/callback laboratory, state, expanders, UI/drawing, audio/MIDI, buffers, custom kernels, profiling, packaging and SDK migration.
4. Generate exhaustive raw/facade reference and C++→Python crosswalk from the verified manifest; add meaningful prose for restrictions, errors and defaults.
5. Extract/test all executable snippets; conceptual pseudocode must be clearly marked and cannot masquerade as a port.
6. Have a new-to-Rack reader execute the quickstart from a clean profile; fix missing prerequisites and record feedback. Keep candidate artifact instructions explicitly pre-release until release validation.

### Validation

- Warning-as-error site and link checks pass; all manifest doc anchors resolve.
- Every executable snippet runs against the built bindings, with in-host tests where necessary.
- Reference has no undocumented verified public symbol; tutorials have no fabricated outputs.

### Acceptance criteria

- A human can install a candidate, create an instrument and troubleshoot failures without reading C++.
- Generated reference supplements rather than replaces explanations.
- All sample provenance/license information is accessible from docs.

## RP-38 — Harden concurrency, security, lifetime and failure recovery

**Priority:** p2 · **Depends on:** RP-22, RP-23, RP-24, RP-28, RP-29, RP-30, RP-32

### Deliverables

- `tests/hardening/`
- `SECURITY.md`
- `docs/explanation/security.md`
- `native runtime fixes`

### Implementation steps

1. Fuzz conversion boundaries, JSON, indices, graph descriptions, callback returns and stale handles within trusted-script assumptions.
2. Run ASan/UBSan/TSan or documented compatible alternatives; separate incompatible host/runtime sanitizer limitations from passing checks.
3. Stress engine/UI concurrency, save/autosave/reset/rate change, menu closure, expander hot-removal, state replacement and interpreter shutdown.
4. Test import-path hijacking, patch-referenced code attempts, dependency/runtime collisions, malformed manifests and unexpected plugin unload.
5. Ensure no C++/Python exception crosses ABI callbacks; verify bounded diagnostics, deterministic silence/bypass and actionable recovery.
6. Document trusted code capabilities and non-sandbox status, distribution/signing risks and private security-reporting process; fix findings, do not merely document unsafe defaults.

### Validation

- 1,000 repeated create/delete and save/reload cycles show no unbounded growth or invalid memory access.
- Time-bounded subprocess stress detects deadlocks and crash regressions.
- Production audio-path instrumentation remains clean under forced slow/maliciously erroneous Python callbacks.

### Acceptance criteria

- All high-severity hardening findings close with regression tests.
- No security claim implies untrusted Python is sandboxed.
- Unavailable sanitizer/platform evidence stays an explicit blocker where required.

## RP-39 — Package and validate Linux artifacts on a clean machine

**Priority:** p2 · **Depends on:** RP-37, RP-38

### Deliverables

- `packaging/linux/`
- `CI Linux release-candidate job`
- `docs/installation/linux.md`
- `artifact evidence`

### Implementation steps

1. Produce a Rack-compatible plugin/runtime bundle and separate developer artifacts from pinned SDK/CPython/toolchain inputs.
2. Inspect exported symbols, dynamic dependencies, rpaths, glibc baseline and runtime isolation; do not assume a manylinux wheel solves Rack plugin loading.
3. Include GPL source/notices and approved dependencies; exclude SDK archives, restricted assets and personal data.
4. Install on a clean Linux x86_64 VM/profile without the development checkout or system Python assumptions.
5. Run all official ports, import/loader errors, save/reopen, browser preview and teardown; retain logs and artifact checksums.
6. Document installation, verification, upgrade/uninstall and version mismatch diagnosis.

### Validation

- Fresh-machine actual Rack load succeeds and required tests execute rather than skip.
- Offline/no-audio-device path works; production audio smoke uses documented environment.
- Rebuilds produce explainable output differences with dependency/source manifests.

### Acceptance criteria

- Linux x86_64 support is backed by real candidate artifacts and host evidence.
- Runtime bundle and developer package roles are clearly distinct.
- No artifact publication is implied or performed without approval.

## RP-40 — Package and validate macOS artifacts with native architecture evidence

**Priority:** p2 · **Depends on:** RP-39

### Deliverables

- `packaging/macos/`
- `CI macOS candidate jobs`
- `docs/installation/macos.md`

### Implementation steps

1. Confirm SDK-supported macOS architectures/deployment targets from RP-02; build each advertised architecture with compatible Python/native dependencies.
2. Inspect Mach-O architectures, install names/rpaths, framework resolution and CPU-specific SIMD behavior.
3. Test CPython embedding and runtime coexistence in real Rack on each target; cross-compilation alone is insufficient.
4. Exercise plugin install, browser preview, rendering, all samples, state, IO fixtures, teardown and window recreation.
5. Document quarantine/codesigning/notarization behavior. Use existing signing credentials only with authorization; unsigned tests must say what they establish.
6. Retain logs, checksums, license manifests and clean-machine installation evidence.

### Validation

- Native runner/clean Mac executes candidate artifacts without dev-environment search paths.
- All mandatory host/sample/concurrency checks pass per advertised architecture.
- Bad-architecture/runtime/codesign cases produce clear recovery guidance.

### Acceptance criteria

- Only natively validated architectures appear in the support table.
- Missing hardware/signing gates remain open, not waived silently.
- No release support inferred from Linux results.

## RP-41 — Package and validate Windows artifacts with explicit loader/runtime rules

**Priority:** p2 · **Depends on:** RP-39

### Deliverables

- `packaging/windows/`
- `CI Windows candidate jobs`
- `docs/installation/windows.md`

### Implementation steps

1. Confirm SDK/toolchain architecture compatibility and CPython runtime distribution constraints; choose one supported ABI set.
2. Implement deterministic DLL search rules and dependency packaging without relying on PATH or arbitrary current-directory DLLs.
3. Validate exports, C++ runtime compatibility, Unicode/long paths, runtime collision detection and install/uninstall behavior.
4. Run real Rack host tests with all official samples, rendering, IO fixtures, save/reopen, menus/expanders and shutdown.
5. Test missing DLL, incompatible Python, second plugin runtime and permission failures; collect actionable diagnostics.
6. Record checksums/notices and clean Windows machine evidence; signing is a separate owner-approved action.

### Validation

- Candidate loads on a clean native Windows runner/VM without development tools on PATH.
- All required sample/lifetime/typing tests pass for the advertised ABI.
- Loader tests reject unsafe search-path fallback and detect mixed runtimes.

### Acceptance criteria

- Windows support rests on native execution, not cross-compile success.
- Bundled libraries have documented redistribution terms.
- Unvalidated ABI/architectures are not advertised.

## RP-42 — Prove complete API coverage and sample parity on all advertised targets

**Priority:** p2 · **Depends on:** RP-31, RP-32, RP-37, RP-38, RP-40, RP-41

### Deliverables

- `release-evidence/coverage/`
- `release-evidence/samples/`
- `docs/reference/coverage.md`

### Implementation steps

1. Regenerate the SDK inventory from the pinned source/configuration on each advertised platform and inspect the diff.
2. Require every supported public declaration to have a verified semantic Python mapping, tests and reference docs; reject NotImplementedError/native-only/unsafe-unreachable stand-ins.
3. Review template generic/extension policies, macros, deprecated methods, virtual overrides, platform branches and external-type adapters specifically for scope cheating.
4. Run differential binding tests and actual-host integration against the exact candidate artifacts, not source-tree imports.
5. Reconcile every official tutorial/guide/template source block and sample expected-output test.
6. Collect independent review from a human or an explicitly authorized agent workflow; resolve findings or keep the full-release gate closed.

### Validation

- Strict coverage checker reports zero unreviewed/planned/unrepresentable public IDs on each advertised target.
- Deliberately removed mappings/tests/docs cause the release check to fail.
- All official sample ports run from clean candidate installs; no hidden skips.

### Acceptance criteria

- Full-surface claim has reproducible declaration, semantic and usability evidence.
- If exact complete coverage is impossible, obtain owner-approved revised scope and call the artifact partial rather than full.
- Review evidence links to immutable candidate refs.

## RP-43 — Verify performance, real-time invariants and long-run stability

**Priority:** p2 · **Depends on:** RP-39, RP-40, RP-41, RP-42

### Deliverables

- `release-evidence/benchmarks/`
- `benchmarks/results/`
- `docs/performance-results.md`

### Implementation steps

1. Run RP-07's frozen A/B suite on controlled hardware using final artifacts and equivalent native modules.
2. Cover representative nontrivial/trivial kernels, rates, channel/module counts, feedback, expanders, engine threading and UI pressure.
3. Measure median/p99/absolute cost, deadline misses, allocation/GIL/refcount instrumentation, startup, copies and memory stability.
4. Run the defined 30-minute stress workload and repeated lifecycle tests; compare clean versus deliberately slow Python UI.
5. Record raw datasets, environment, repetitions, uncertainty, power policy, compiler flags and exact refs; keep laboratory callback numbers separate.
6. Investigate regressions rather than weakening budgets; any budget change needs measured justification and owner approval.

### Validation

- Production DSP shows zero forbidden Python/GIL/heap events and attributable deadline misses in specified tests.
- Performance budgets pass on each claimed target/workload or remain explicit failures.
- Independent rerun reproduces conclusions within documented uncertainty.

### Acceptance criteria

- Reasonably performant claim is backed by numbers and native baselines.
- No timing claim is based only on shared CI averages.
- All reported limitations and test conditions are published.

## RP-44 — Prepare the audited release and obtain approval before publishing

**Priority:** p2 · **Depends on:** RP-01, RP-03, RP-37, RP-38, RP-42, RP-43

### Deliverables

- `CHANGELOG.md`
- `release-evidence/manifest.json`
- `versioned docs`
- `candidate artifacts`

### Implementation steps

1. Recheck live Witan state, dependency closures, clean git/worktrees, CI and immutable candidate commits; recover project metadata if still missing.
2. Audit README/docs/metadata claims against coverage, platform, sample, security and benchmark evidence. Use preview labeling for any intentionally partial candidate.
3. Verify versioning against Rack manifest conventions, dependency versions, licenses/source availability, SBOM, checksums and reproducibility notes.
4. Test installation/upgrade/uninstall on all supported systems from final artifact bytes and publishable docs.
5. Present owner with exact artifacts, release notes, unresolved decisions, signing/library-submission needs and proposed GitHub/PyPI/docs publication actions.
6. Only after approval, publish/tag/upload using protected CI, then fetch public artifacts and verify checksums/import/host behavior. Close epic/project only after delivered outcome is verified.

### Validation

- Release checklist and all mandatory CI/host/coverage/docs/performance checks are green for exact refs.
- Downloaded published artifacts match approved checksums and clean-machine smoke tests.
- No secret, restricted artwork, unlicensed dependency or fabricated support claim is present.

### Acceptance criteria

- Release publication has explicit owner approval.
- Full API claim is made only if RP-42 actually passed.
- Witan outcome and human-facing release docs accurately distinguish verified from inferred support.
