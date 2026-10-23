# RP-14 bounded implementation slices

These child contracts supplement, not replace, [RP-14 in TASKS.md](TASKS.md#rp-14--bind-module-configuration-lifecycle-bypass-and-native-event-adapters).
Live Witan status is authoritative. RP-14 is blocked by **all three** children;
RP-14B waits for A and RP-14C waits for B. Existing RP-14 downstream dependencies
are unchanged. The implementation phase is not complete when one slice closes.

## RackPy 14A: checked Module construction and component access

**Witan slug:** `tk-rackpy-14a-checked-module-construction-and-compo-131230`

Bounded RP-14 slice. Read AGENTS and full RP-14 contract/comments; claim with real session ID. Deliver native/module/, native/bindings/engine/module.cpp, src/rackpy/engine/module.py, strict stubs, tests/engine/test_module.py and in-Rack suite, manifest and human docs. Bind Module construction as a scoped factory transaction: config once, nonnegative counts, rollback and invalidate leaked handles on exceptions, seal configuration after return. Preserve real Rack default components. Bind getNum/getParam/getInput/getOutput/getLight and fixed-size params/inputs/outputs/lights views, checked IDs, stable component identity/liveness and native bypass-route configuration with duplicate-output rejection. No Python production virtual trampolines; Python callback is construction only. Map destructor/constructor/config/getters/vectors/configBypass/BypassRoute records; assign EVERY remaining Module declaration explicitly to RP-14B/C or existing RP-15/16/17/22/23 without promoting deferred mappings. Tests must run in actual Rack, include native reference comparison, 1000 churns, stale retained views, wrong thread, repeated/late config, zero-count config, invalid indices, partial constructors, and constructor callback reentry. Default helpers such as configParam may remain planned until complete template semantics in RP-14B. Test-only hook may validate bypass routing offline; it is not Python callback coverage. Run prek, strict typing, relevant native/host/introspection, coverage audit, docs; leave commit/checkpoint. Does not close RP-14.

## RackPy 14B: Module parameter and component metadata mappings

**Witan slug:** `tk-rackpy-14b-module-parameter-and-component-metada-040154`

Remaining RP-14 construction/metadata slice; inherits full RP-14 contract/comments and AGENTS validation. Implement configParam/configSwitch/configButton/configInput/configOutput/configLight full class-factory template semantics, including Python subclasses where thread-safe, native-only mixed ParamQuantity value/range methods with explicit later laboratory semantics, checked IDs/ranges and metadata replacement invalidation. Bind all ParamQuantity/SwitchQuantity declarations (JSON deferred explicitly RP-15), metadata arrays and lookup, PortInfo/LightInfo::module fields. Preserve ownership/null semantics and identity; reject wrong-phase calls/repeated unsafe config; exception-safe metadata ownership transfer. Differential tests: display scales/log/exponential, labels/defaults/snap/button flags, smoothing/immediate semantics, name/description overrides, bad ranges, callback errors; real Rack construct/reset/randomize/save/reload tests coordinated with RP-14C/RP-15. Native-only helpers do not count complete Python virtual/template semantics. Update manifest/tests/types/human docs, run full relevant checks and commit. RP-14 remains blocked until this and C pass.

### RP-14B subdivisions

RP-14B is additionally blocked by both children below; B2 waits for B1.
This bounds the ownership groundwork without declaring native built-ins to be
complete template mappings. All original B acceptance criteria remain in force.

**RP-14B1 — checked built-in port and light metadata views**
(`tk-rackpy-14b1-checked-built-in-port-and-light-meta-82183d`). Implement
constructor-only built-in `config_input/output/light`, `get_*_info`, fixed
metadata collections and null light slots. Distinct borrowed view adapters
preserve per-slot generation identity and weak module backreferences, validate
IDs/thread/phase, invalidate on replacement/deletion/failed construction, and
prepare replacements before retiring old metadata. Do not alter standalone
PortInfo/LightInfo subclass semantics or claim full template/raw-field coverage.
Native differential tests cover names/descriptions/IDs/types/backreferences;
actual Rack tests cover startup/live UI, 1,000 replacements and owner collections,
stale/wrong-thread/invalid-input/rollback paths. Native preparation-failure test,
strict stubs, executable snippet, prose, manifest notes, hooks/native/host/
introspection/coverage/docs and a commit/checkpoint are required. Closes only B1.

**RP-14B2 — quantity mappings and complete metadata class factories**
(`tk-rackpy-14b2-quantity-mappings-and-complete-metad-bb5ae1`). Complete all
ParamQuantity/SwitchQuantity records (JSON remains RP-15), `configParam/Switch/
Button`, quantity lookup/arrays, and full `configInput/Output/Light` class factories
including Python subclasses, exception-safe native ownership transfer and stale
view handling. Complete standalone metadata module fields and raw identity-field/
vector semantics that checked adapters do not provide. Mixed-thread value/range
virtuals must be native in production; RP-22 owns explicit laboratory semantics,
not a silently omitted callback. Validate ranges, display scaling/log/exponential,
labels/defaults/snap/button/smoothing/immediate behavior, overrides/errors;
coordinate actual-host reset/randomize/save/reload with RP-14C/RP-15. Complete
original B mapping/type/doc/test/validation/commit obligations. B/14 remain blocked
until their original acceptance criteria are fully evidenced.

### RP-14B2 subdivisions

RP-14B2 waits for **both** children below; B2b waits for B2a. Parent B2, B and
RP-14 retain all original acceptance criteria, including complete template and
callback semantics. This is not completion of the implementation phase.

**RP-14B2a — checked built-in parameter quantity construction and views**
(`tk-rackpy-14b2a-checked-built-in-parameter-quantity-8643c7`). Implement
constructor-only built-in `config_param/switch/button`, checked IDs, finite
increasing bounds/defaults and display coefficients, prepare-before-replace,
per-quantity generations, fixed quantity collections and weak-backreference
views. Preserve default Param values, native scalar/text metadata and copied
switch labels. Expose native value/display/reset/randomize calls only with a
live bound Rack UI context, guarded across GIL release; no Python trampoline.
Unregistered modules must reject smoothing-target writes: Rack's Engine retains
a raw module pointer, so full smoothing requires engine ownership/lifecycle
cleanup in B2b/C/RP-16. Immediate/display writes remain available. Test SDK
native reference behavior **inside actual Rack**, startup context refusal,
1,000 replacements and owner churns, stale and wrong-thread/context access,
invalid inputs/phase, rollback and native preparation failure. Strict stubs,
executable snippet, human docs, manifest and full relevant validation/commit
are required. Exact bounded manifest set: `field`/`method`/`record` entries for
`rack::engine::ParamQuantity` and `SwitchQuantity` except JSON, plus
`Module::paramQuantities`, `getParamQuantity(int)` and the three
`configParam/Switch/Button` method templates. These entries remain partial and
`planned`, not completed mappings: no standalone constructors, raw identity or
vector writes, Python virtuals, complete class factories or engine-owned smoothing.
JSON remains untouched under RP-15. Default quantities are non-null after config;
raw null-slot insertion semantics remain B2b.

**RP-14B2b — complete quantity and metadata subclass factories and raw semantics**
(`tk-rackpy-14b2b-complete-quantity-and-metadata-subc-ff231c`). Complete every
original B2 obligation absent from B2a: standalone ParamQuantity/SwitchQuantity,
full quantity and port/light class factories with Python subclasses where safe,
exception-safe ownership transfer and stale retained direct objects, standalone
metadata module fields, raw identity/vector/labels mutation and null semantics.
All quantity virtuals need explicit native production vs RP-22 laboratory paths;
UI-safe overrides must not silently disappear. Full smoothing must coordinate
engine ownership and deletion with C/RP-16; reject unsafe unregistered targets,
never retain dangling module pointers in Engine. JSON remains RP-15. Parent
contracts still require native differential and actual-host tests, errors,
reset/randomize/save/reload coordination, mappings/types/docs, validation and
commits. The built-in child cannot close B2/B/14.

### RP-14B2b bounded groundwork

RP-14B2b additionally waits for the children below; completing them leaves all other
original B2b obligations on the parent. No backwards dependencies on C/16 are
introduced and no original acceptance criterion is relaxed.

**RP-14B2b1 — borrowed quantity helpers and mutable switch-label views**
(`tk-rackpy-14b2b1-borrowed-quantity-helpers-and-muta-924aae`). Expose inherited
Quantity helpers on ParamQuantityView: get_string/is_min/is_max/set_min/set_max/
toggle/move_value/get_range/is_bounded/to_scaled/from_scaled/set_scaled_value/
get_scaled_value/move_scaled_value. Keep native behavior, context/lifetime/thread
checks and reject indirect smoothing-target writes on unregistered modules.
Replace copied SwitchQuantityView.labels reads with a checked mutable vector
alias supporting indexed assignment/deletion, insert/append/extend/clear,
negative indices, whole-sequence replacement and copied iteration/tolist.
Resolve storage afresh so reallocations/reassignments preserve aliases, while
slot replacement, deletion and rollback invalidate them. No owner retention.
Test native SDK differential helper/value/label behavior inside actual Rack,
startup/live UI context, invalid inputs, failure atomicity, stale/wrong-thread
aliases and 1,000 replacements/owner collections. Update strict types, runnable
snippet, prose, mapping notes and validation evidence; commit/checkpoint.
Exact manifest scope: `field:rack::engine::SwitchQuantity::labels` stays planned
because standalone semantics remain absent. The 14 inherited `rack::Quantity`
method records already mapped by RP-12 gain evidence/notes for borrowed adapters,
not new counted declarations or promotions. Full factories, standalone objects,
metadata module/identity/vector fields, smoothing ownership and laboratory
virtual semantics remain B2b/RP-22. This child does not close B2b/B2/B/14 or the
implementation phase.

**RP-14B2b2 — lifetime-safe standalone metadata module backreferences**
(`tk-rackpy-14b2b2-lifetime-safe-standalone-metadata--3566d5`). Exact scope:
`field:rack::engine::PortInfo::module` and
`field:rack::engine::LightInfo::module`. Add nullable mutable weak backreferences
to live RackPy Modules, preserving identity without owning the module or
transferring metadata into its slots. Clear actual native pointers before owner
teardown/GC/constructor rollback, preserve reassignment and failure atomicity,
prune expired native registrations, and synchronize standalone metadata deletion
on another thread without Python work in module cleanup. Field access is
main-thread-only; closed/wrong-type assignments leave the old link intact.
Validate native pointer clearing/reassignment/pruning/deletion races, real Rack
startup/live UI native differential/null/identity/subclass/error/thread/rollback
and 1,000 owner/metadata churns. Update types, executable snippet, prose, exact
manifest entries, generated evidence and full relevant checks; commit/checkpoint.
This child does not complete class factories, raw metadata collections, quantity
subclasses, smoothing ownership or callback-laboratory semantics. Those remain
on B2b; B2/B/14 and the implementation phase retain their original exit gates.

**RP-14B2b3 — unattached standalone quantities and UI overrides**
(`tk-rackpy-14b2b3-unattached-standalone-quantities-a-449390`). Real SDK
ParamQuantity/SwitchQuantity constructors and Quantity inheritance, native
null-module value/no-op semantics, scalar/text/paramId fields, owned mutable
SwitchLabels aliases and all UI-safe virtuals (including getDescription).
Mixed value/range overrides remain refused in production; RP-22 owns their
laboratory semantics. Guard native access on the main thread, preserve raw
unbounded/equal bounds, conversion atomicity, owned alias lifetime and default
label/index overflow safety. Compare unattached SDK reference results and run
actual Rack startup/live UI tests, callback/base/error/thread/null paths and
1,000 owner/alias churns. Strict types, executable snippet, prose, manifest
notes, full relevant checks and commit/checkpoints are required. Exact manifest
scope: the 41 field/method/record ParamQuantity/SwitchQuantity entries except
JSON, all still `planned`. `module` remains read-only `None`: attachment,
transfer, complete factories, raw module collections and engine-owned smoothing
are not implemented. B2b waits for this child too; all remaining original B2b
and parent obligations remain intact. This does not complete the implementation
phase.

**RP-14B2b4 — lifetime-safe attached standalone quantities**
(`tk-rackpy-14b2b4-lifetime-safe-attached-standalone--e02465`). Complete the
nullable mutable weak `ParamQuantity`/`SwitchQuantity.module` field and mutable
`param_id`; preserve SDK `get_param` identity and null-on-invalid-ID semantics.
Guard attached native immediate/value/display/helper/reset/randomize calls with
checked IDs and a live Rack UI context. UI-safe overrides and base/error paths
remain available. Reuse synchronized native backreference registration to clear
the SDK module pointer on close, GC and constructor rollback without retaining a
Module. Standalone quantity deletion on another thread detaches safely. Refuse
smoothing-target writes for unregistered quantities; do not transfer into Module
slots or register with the engine. Keep all 41 non-JSON quantity manifest entries
`planned`: full factories/raw module collections/engine smoothing stay on B2b,
mixed callbacks on RP-22, JSON on RP-15. Validate actual-host differential
behavior, invalid IDs/null results, wrong thread/context/type/closed assignment,
reassignment, rollback, owner GC, callback reentry/errors and 1,000 churns.
Update strict types, runnable snippet, human docs and mapping evidence; run
relevant hooks/native/host/introspection/coverage/docs checks and commit with a
checkpoint. This child does not close B2b/B2/B/14 or the implementation phase.

**RP-14B2b5 — transferred port and light metadata class factories**
(`tk-rackpy-14b2b5-transferred-port-and-light-metadat-b11611`). Exact scope: the
`configInput/configOutput/configLight<type>(int, std::string)` method templates
and `getInputInfo/getOutputInfo/getLightInfo(int)`. An `info_type` argument names
a `PortInfo`/`LightInfo` class or Python subclass; RackPy constructs it with no
arguments on the main thread inside the constructor callback, as Rack's
`new TInfo` does, requires a new instance of exactly that class, rechecks the
phase and ID after the reentrant constructor, transfers native ownership into
the slot (counted pybind11 disown) and sets the fields Rack's template sets.
Getters and fixed collections return the same Python object. Replacement,
constructor rollback and module close/GC reclaim and retire it: `alive` is
false and every bound access raises `StaleHandleError`. Transferred objects are
main-thread-only; `get_name`/`get_description` overrides keep `ui` semantics,
with errors contained when Rack calls without the GIL. `info_type=None` keeps
the RP-14B1 views. Validate a native `configInput<CustomInfo>` differential and
replacement, actual-host startup/live-UI suites, wrong classes/constructors,
foreign/owned/released `__new__` results, reentrant configuration, wrong
thread/phase, rollback atomicity and 1,000 replacements/owner churns with the
transferred count back to baseline. Raw metadata vector mutation and null
insertion, quantity factories, engine-owned smoothing and RP-22 laboratory
semantics remain on B2b. This child does not close B2b/B2/B/14 or the
implementation phase.

**RP-14B2b6 — transferred parameter quantity class factories**
(`tk-rackpy-14b2b6-transferred-parameter-quantity-cla-5ebbcc`). Exact scope: the
`configParam/configSwitch/configButton<type>` method templates and
`getParamQuantity(int)`. A `quantity_type` argument names a `ParamQuantity`
(switch/button: `SwitchQuantity`) class or Python subclass. Rack's arguments are
validated first; RackPy then constructs the class inside the constructor
callback with the B2b5 exact-type, ownership and reentrancy checks, transfers it
into the slot, sets exactly the fields Rack's templates set and the Param
default. Getters return the same object; replacement, close, GC, worker drop and
rollback retire it, including its owned labels alias. Python callers keep the
main-thread, live-context and unregistered-smoothing rules; Rack callers without
the GIL get native mixed behaviour that never throws and contained UI overrides.
Built-in switch/button configuration also stops resetting fields Rack's
templates leave alone. Validate native `configParam/Switch/Button<Custom>`
differential and replacement, actual-host startup/live-UI suites, wrong
classes/constructors, validation before construction, reentrancy, wrong
thread/context, overrides closing their module, Rack-thread mixed reads and
no-GIL UI dispatch through test hooks, and 1,000 replacements/owner churns with
the transferred count back to baseline. The raw `paramQuantities` vector, the
ParamQuantity/SwitchQuantity records (RP-22 mixed overrides, engine smoothing),
JSON (RP-15) and smoothing ownership remain open. This child does not close
B2b/B2/B/14 or the implementation phase.

**RP-14B2b7 — constructor-scoped raw module vectors**
(`tk-rackpy-14b2b7-constructor-scoped-raw-module-vect-f0af27`). Owner decision
2026-10-23: constructor-scoped full mutation. Exact scope: the `params`,
`inputs`, `outputs`, `lights`, `paramQuantities`, `inputInfos`, `outputInfos`
and `lightInfos` fields. Inside the constructor callback after `config`, the
collections take Rack's raw vector edits (item assignment, `del`, `insert`,
`append`, `clear`, `resize`), with per-slot generations kept in step; metadata
slots take a Python-created object or `None` without setting fields; raw
removal releases transferred objects (usable, tracked module link) and deletes
native-created metadata; a structural edit of a component vector invalidates
only that vector's views. Identity fields are constructor-only. The seal
validates sizes, non-null quantities and port metadata, each slot's module,
ID and port type, and bypass routes, failing construction otherwise. After the
seal only component item assignment remains. Validate a native raw-edit
differential, seal rejections, transfer/release semantics, copy semantics and
per-vector invalidation, constructor-only and thread rules, identity freezing,
rollback and 1,000 edit/owner churns. `bypassRoutes` stays RP-14C; JSON RP-15;
smoothing ownership and RP-22 laboratory semantics remain on B2b. This child
does not close B2b/B2/B/14 or the implementation phase.

**RP-14B2b8 — reconcile ParamQuantity and SwitchQuantity records**
(`tk-rackpy-14b2b8-reconcile-paramquantity-and-switch-7b7350`). Promote the 34
records whose semantics B2a–B2b7 now provide (fields, labels, UI and
ui-exclusive virtuals, immediate values, `getParam`, both records), binding
ParamQuantity's and SwitchQuantity's own UI overrides on their classes. Keep
seven planned with explicit owners: the mixed value/range virtuals to RP-22
(native path done; coverage policy 11 needs the laboratory path) and
smoothing (`setValue` with smoothing, deprecated `get/setSmoothValue`) to
RP-14C with RP-16, since Rack's `Engine::removeModule` cancels smoothing for
registered modules. JSON stays RP-15. Closing this child lets RP-14B2b close;
B2, B and RP-14 keep their own acceptance reviews.

## RackPy 14C: native lifecycle events and identity adapters

**Witan slug:** `tk-rackpy-14c-native-lifecycle-events-and-identity--929845`

Remaining RP-14 lifecycle/identity slice; inherits full RP-14 contract/comments and AGENTS validation. Implement Module identity/model handle/IDs (coordinate RP-16), isBypassed and production native adapters for onAdd/onRemove/onBypass/onUnBypass/onPortChange/onReset/onRandomize/onSampleRateChange/onSave/onSetMaster/onUnsetMaster plus deprecated overloads and immutable event snapshots. Preserve exact Rack superclass behavior (RandomizeEvent, not ResetEvent), synchronous native handlers, bounded lock-free snapshot observation on UI; never Python, refcount, allocation/destruction, locks, logging or IO in driver/production audio paths. Mark semantic Python engine/driver callback access as planned RP-22 laboratory, not completed by observation. Integrate actual Rack engine ownership with constructor state seal and deletion invalidation; browser preview None; test lifecycle exceptions, engine reentry refusal, queue overflow, sample-rate/master driver-side dispatch, reset/randomize/bypass and multichannel routes against native fixture, save/reload ordering (RP-15). Native/actual-host validation incl. audio-path guard, strict types, manifest/docs, full relevant hooks and commit. Parent RP-14 closes only when all original acceptance checks are evidenced.

### RP-14C subdivisions

RP-14C is additionally blocked by its children; all original C acceptance
criteria remain on the parent.

**RP-14C1 — module identity, bypass query and deprecated constructor**
(`tk-rackpy-14c1-module-identity-bypass-query-and-dep-6e3021`). Exact scope:
`Module::id`, `getId()`, `isBypassed()` and the deprecated
`Module(int, int, int, int)` constructor. `id` is Rack's int64 (-1 until an
engine assigns one), writable only in the constructor callback; `is_bypassed()`
is False until an engine owns the module; the deprecated constructor is a
warning `Module.create` overload that configures first. `model`/`getModel()`
move to RP-16 (Model handles); `~Module`, engine ownership and lifecycle events
stay on later 14C children.

**RP-14C2 — engine ownership, Rack-side deletion and engine smoothing**
(`tk-rackpy-14c2-engine-ownership-rack-side-deletion--15472f`). Exact scope:
`~Module` plus the deprecated `ParamQuantity::setSmoothValue/getSmoothValue`
(setValue's smoothing half). A sealed module can be released to Rack's engine:
its handles borrow it through its token and keep their identity, `close()`
and GC no longer delete it, and Rack deletes it after `Engine::removeModule` on
the main thread without the GIL, invalidating every token and retiring
transferred metadata. Rack can hand the module out again after every handle
went. `module::smoothingAllowed` is true exactly for engine-owned modules. No
public API adds modules to the engine before RP-16/RP-24; test hooks play
Rack's part, with a test-only Model for autosave.

**RP-14C3 — native lifecycle event adapters and snapshots**
(`tk-rackpy-14c3-native-lifecycle-event-adapters-and--be0036`). Exact scope:
the eleven `Module` event records and the native adapters of the eleven
lifecycle events plus their deprecated no-argument forms. Each adapter runs
Rack's behaviour, then records a snapshot in a bounded lock-free ring in a
Python-free translation unit (audio-symbol checked); exclusive-lock events
refuse engine re-entry from Python run inside. Python drains snapshots
(`lifecycle_events()`, drop-and-count overflow) and may call a handler
directly on a sealed Python-owned module. Event records are implemented; the
`on*` records stay planned for Python overrides (RP-16 module classes for UI
events, RP-22 for driver events).

**RP-14C4 — constructor-scoped bypass routes and engine bypass routing**
(`tk-rackpy-14c4-constructor-scoped-bypass-routes-and-3eab37`). Exact scope:
`Module::bypassRoutes`, under the owner's constructor-scoped raw-vector
decision: reads are copies, raw edits only in the constructor callback after
`config`, the seal rejects out-of-range routes, and real-engine bypass routing
through cables with 0, 1 and 16 channels matches Rack's `processBypass`.
