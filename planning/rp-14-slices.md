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

## RackPy 14C: native lifecycle events and identity adapters

**Witan slug:** `tk-rackpy-14c-native-lifecycle-events-and-identity--929845`

Remaining RP-14 lifecycle/identity slice; inherits full RP-14 contract/comments and AGENTS validation. Implement Module identity/model handle/IDs (coordinate RP-16), isBypassed and production native adapters for onAdd/onRemove/onBypass/onUnBypass/onPortChange/onReset/onRandomize/onSampleRateChange/onSave/onSetMaster/onUnsetMaster plus deprecated overloads and immutable event snapshots. Preserve exact Rack superclass behavior (RandomizeEvent, not ResetEvent), synchronous native handlers, bounded lock-free snapshot observation on UI; never Python, refcount, allocation/destruction, locks, logging or IO in driver/production audio paths. Mark semantic Python engine/driver callback access as planned RP-22 laboratory, not completed by observation. Integrate actual Rack engine ownership with constructor state seal and deletion invalidation; browser preview None; test lifecycle exceptions, engine reentry refusal, queue overflow, sample-rate/master driver-side dispatch, reset/randomize/bypass and multichannel routes against native fixture, save/reload ordering (RP-15). Native/actual-host validation incl. audio-path guard, strict types, manifest/docs, full relevant hooks and commit. Parent RP-14 closes only when all original acceptance checks are evidenced.
