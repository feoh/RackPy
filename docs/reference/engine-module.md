# Module construction and components

**Implemented slices: RP-14A, built-in port/light metadata adapters (RP-14B1),
checked built-in quantities (RP-14B2a), and borrowed quantity helpers / mutable
switch-label views (RP-14B2b1), standalone metadata backreferences (RP-14B2b2),
and unattached standalone quantities (RP-14B2b3), Linux x86_64 inside Rack Free 2.6.6.**
This is not a registerable instrument yet. Full quantity/metadata class factories
and engine-owned smoothing (RP-14B2b), native lifecycle
adapters (RP-14C), plugin/model factories (RP-16), DSP (RP-17), the Python callback
laboratory (RP-22), and expanders (RP-23) remain planned. See the
[coverage report](coverage.md) for declaration-level status.

## Construction transaction

`rackpy.engine.Module` and `rackpy.raw.engine.Module` are the same native
binding. `Module.create(configure=None)` constructs an owned, empty Rack module.
When supplied, `configure(module)` runs synchronously on Rack's main thread.
Its return value is ignored; the factory returns the same Python handle it
passed to the callback. Only that callback may call `config`, `config_bypass`,
`config_input`, `config_output`, `config_light`, `config_param`, `config_switch`
and `config_button`. These are construction
operations, not engine commands.

`config(num_params, num_inputs, num_outputs, num_lights=0)` calls Rack's
implementation once. Counts must be nonnegative. Even `config(0, 0, 0)`
consumes the one call: a second call raises `OwnershipError` before Rack's
assertion. Invalid counts are rejected before native configuration and can be
corrected within the same callback. A failure partway through native allocation
closes the module; it cannot be reused even if the callback catches the error.

The factory seals the module before returning it. Late configuration raises
`OwnershipError`. Nested factories have independent construction scopes.
`Module.create()` without a callback returns a sealed, empty module. The
deprecated four-count C++ constructor is not mapped yet.

Here is a runnable construction-only example. The documentation snippet tests
run this exact file inside Rack:

```{rackpy-example} module_construction.py
```

This does not add the module to Rack's engine or install a DSP callback. A Python
subclass or an attribute named `process` is **not** a DSP implementation.
There is no Module virtual trampoline in this slice. The construction callback
is never stored in the native module, and no audio path invokes it.

## Component access

`get_num_params/inputs/outputs/lights()` and
`get_param/input/output/light(index)` call Rack's corresponding getters.
Getter IDs must satisfy `0 <= index < count`; invalid IDs raise `IndexError`
instead of accessing an unchecked C++ vector.

`params`, `inputs`, `outputs` and `lights` are fixed-size checked views.
They support length, iteration and integer indexing (including Python's
negative indices). They do not support resizing, slicing, replacement or
named lookup. Setting a component's value writes through to the actual Rack
component. Repeated lookups of a live component preserve Python identity.
Iteration snapshots the component *handles*, not their changing values.

A checked, non-resizable view does not expose every operation of C++'s public
mutable vector fields. Those four manifest records therefore remain
`planned`, not completed mappings. Named lookup remains planned.
For port channel and buffer rules, see [Engine components](engine-components.md).

(module-metadata)=
## Built-in port and light metadata

`config_input(port_id, name="")`, `config_output(port_id, name="")` and
`config_light(light_id, name="")` configure **built-in** Rack metadata during
construction. They return checked `PortInfoView` / `LightInfoView` adapters.
IDs must be nonnegative and below the corresponding component count.

`get_input_info(index)`, `get_output_info(index)` and `get_light_info(index)`
return the same live adapters. Rack initializes input/output metadata in
`config`; light slots are `None` until explicitly configured. `input_infos`,
`output_infos` and `light_infos` are fixed-size `Components` collections with
negative indexing and iteration, not replaceable or resizable raw vectors.

The adapters expose mutable `name` and `description`, `get_name()` and
`get_description()`. A port also has `get_full_name()` (Rack appends " input"
or " output"), read-only `type` and `port_id`; a light has read-only `light_id`.
The read-only `module` property returns the original owning Python handle.
All native metadata access requires the main thread, including null-slot
lookups. `alive` is a liveness query and may be read from any thread.

Configuring the same slot again is permitted **during construction**. The old
adapter immediately becomes stale; it cannot read a replacement that happens
to reuse its address. Only that slot's generation changes. Replacement prepares
its native object, strings and generation before retiring the previous one, so
a preparation exception leaves the old metadata intact. Closing the module,
dropping its last handle, or failing construction invalidates every adapter,
including retained collections. Views do not keep the module alive; explicitly
retaining `view.module` does, just like retaining the original module handle.

```{rackpy-example} module_metadata.py
```

**Deliberately partial:** borrowed adapters are distinct from standalone,
subclassable `PortInfo` / `LightInfo`. These helpers do not yet accept a class
argument or transfer Python subclasses into native ownership. Arbitrary
identity-field/module-pointer mutation on these borrowed adapters and raw vector
mutation are also absent. Standalone metadata backreferences are documented in
[Engine components](engine-components.md#portinfo-and-lightinfo). RP-14B2 retains these obligations,
along with full ParamQuantity/SwitchQuantity and their mixed-thread virtuals.
Accordingly the template, getter and raw-field manifest records remain
`planned`: built-in adapters alone are not complete public semantics. No
production callback dispatches to Python in this slice.

(standalone-quantities)=
## Unattached standalone quantities

`rackpy.engine.ParamQuantity` and `SwitchQuantity` are aliases of the corresponding
`rackpy.raw.engine` classes. They construct real, Python-owned SDK objects and
inherit `rackpy.raw.Quantity`; a SwitchQuantity also inherits ParamQuantity.
They are **unattached**, not a replacement for `Module.config_param` or an
installable parameter factory. `module` is currently read-only and always `None`;
`get_param()` returns `None`. Module attachment, ownership transfer, raw vector
insertion and engine-owned smoothing remain RP-14B2b work. These partial mappings
stay `planned` in the manifest, including their record entries.

Rack's null-module semantics are important: these quantities do **not** store a
value. `get_value()` and `get_immediate_value()` return zero, and native setters,
reset and randomize do not change it. This also holds when `smooth_enabled` is
true: with no module, Rack never reaches its engine. For a standalone quantity
that actually stores a value, use `rackpy.types.ValueQuantity` instead.

The same scalar/text fields listed below for borrowed quantities are mutable,
as is `param_id` (initially -1). Assigning fields does not initialize a Param.
Raw equal/unbounded ranges retain SDK behavior. `get_label()` supplies Rack's
numbered default when `name` is empty. The maximum signed 32-bit `param_id` is
storable, but requesting that default label raises `ValueError` rather than
letting Rack overflow `paramId + 1`. Explicit names still work. Switch display
strings reject nonfinite/out-of-int-range label indices before Rack's cast.

Python subclasses may override the UI-safe virtuals `get_display_value`,
`set_display_value`, `get_display_precision`, `get_display_value_string`,
`set_display_value_string`, `get_label`, `get_unit`, `get_string`, `reset`,
`randomize`, and `get_description`. Native virtual dispatch reaches those
methods; `super()` reaches Rack's implementation. Errors propagate to Python
callers. Mixed value/range virtuals cannot be Python overrides in production:
subclass definitions of `get_value`, `set_value`, `get_min_value`, `get_max_value`
or `get_default_value` raise `TypeError`, like Quantity. RP-22 retains their
explicit laboratory semantics. These standalone aliases are UI-only and are
never installed in a Module or visited by the production engine.

All native access, construction and field operations require Rack's main thread.
Python code in an override is ordinary user code, not a sandbox or an audio
callback. Inherited helper methods and direct base-class calls use the same
native guards. Deprecated smooth-value methods emit `DeprecationWarning`.
JSON methods remain RP-15 and are not exposed here.

`SwitchQuantity.labels` uses the mutable `SwitchLabels` vector interface described
below, including atomic conversion before assignment/extension and copied
iteration. Unlike a borrowed module alias, an **owned** label alias keeps its
standalone quantity alive. Its ownership edge is visible to Python's garbage
collector, so a subclass retaining its own label alias does not leak a cycle.
Replacing the vector's contents preserves retained aliases; deleting the alias
eventually releases that ownership. This storage
never enters the production audio path.

```{rackpy-example} standalone_quantities.py
```

## Built-in parameter quantities

`config_param(param_id, min_value, max_value, default_value, name="", unit="",
display_base=0, display_multiplier=1, display_offset=0)` configures a built-in
native ParamQuantity. `config_switch(param_id, min_value, max_value,
default_value, name="", labels=())` configures a native SwitchQuantity with
snapping enabled and smoothing disabled. `config_button(param_id, name="")`
uses bounds 0–1 and default 0, also disabling module randomization of the button.
All three helpers are constructor-only. Each writes the native Param's default,
prepares replacements before retiring old quantities, and invalidates only the
replaced quantity view, not the Param component itself.

IDs are checked. Helper bounds must be finite and strictly increasing, with a
finite default inside the range. Display coefficients must be finite; bases
1 and -1 are rejected because they make the inverse undefined. These checked
helpers do not cover arbitrary unbounded/raw C++ configuration semantics.

`get_param_quantity(index)` and the fixed-size `param_quantities` collection
return stable `ParamQuantityView` adapters (or `SwitchQuantityView` for switches
and buttons). Rack's `config` initializes *every* parameter's quantity with
bounds 0–1 and default 0; unlike light metadata, these slots are not initially
null. Empty modules have empty collections. Negative indexing works only on the
collection, not on getter/helper IDs. The views have no standalone constructor,
no Python virtual trampoline, and do not inherit `rackpy.raw.Quantity`.

Quantity views expose these native scalar/text fields for reading and writing:
`min_value`, `max_value`, `default_value`, `name`, `unit`, `display_base`,
`display_multiplier`, `display_offset`, `display_precision`, `description`,
`reset_enabled`, `randomize_enabled`, `smooth_enabled`, and `snap_enabled`.
Direct field assignments, like C++, do not reset the Param or revalidate the
whole range. Keep the metadata coherent; use the checked construction helpers
for initial configuration. `param_id` and `module` are read-only. `get_param()`
returns the same checked Param as the owning module. `alive` remains a
thread-independent liveness query. All other access requires the main thread.
Replacement, failed construction, close and owner collection invalidate views;
retaining a view does not keep the module alive.

Metadata methods `get_min_value`, `get_max_value`, `get_default_value`,
`get_display_precision`, `get_label`, `get_unit` and `get_description` work
without an engine. `get_label()` falls back to a numbered label for empty names.

(borrowed-quantity-helpers)=
### Borrowed quantity helpers and switch labels

The views also expose Rack's inherited `Quantity` helpers without pretending to
inherit the standalone Python `Quantity` class. `get_range()`, `is_bounded()`,
`to_scaled(value)` and `from_scaled(scaled_value)` work without an engine. They
preserve Rack's scaling rules, including equal bounds (scaled value 0) and
unbounded ranges (identity conversion), and do not clamp conversion results.

`SwitchQuantityView.labels` returns a **mutable `SwitchLabels` alias**, not a
copied list. Integer reads/writes/deletion, negative indices, `insert`, `append`,
`extend` and `clear` operate on the native vector. Insertion clamps an
out-of-range index as Python lists do; other indexed operations raise
`IndexError`. Slicing is not supported. Assigning a complete string sequence to
`quantity.labels` replaces the vector's contents. A retained alias sees that
replacement and remains valid across native vector reallocations. String
conversion failures do not partially apply a replacement or extension.

`labels.tolist()` and `list(labels)` return independent copies. Iteration takes
a snapshot when the iterator is created; that copied iterator remains usable
after vector edits or owner deletion. Operations on the **alias itself** always
check its quantity's slot generation and main-thread rule. Replacing the
quantity, failing construction, closing the module or dropping its final handle
makes retained aliases stale. Label aliases do not keep the module alive.
This adds borrowed vector editing, not standalone SwitchQuantity or full
metadata-array mutation semantics.

Value/display methods require Rack's **live bound UI context**. Calling them
at plugin startup, or from a foreign context, raises `ContextError` rather than
dereferencing a missing engine. They pin the module across GIL release and take
a context teardown lease before calling Rack's native implementation:

- `get_value` / `set_value`, `get_immediate_value` / `set_immediate_value`;
- `get_display_value` / `set_display_value`, `get_display_value_string` /
  `set_display_value_string` (including Rack's expression parser);
- `reset` and `randomize`; deprecated `get_smooth_value` / `set_smooth_value`
  aliases emit `DeprecationWarning`;
- `get_string`, `is_min`, `is_max`, `get_scaled_value`, `set_min`, `set_max`,
  `toggle`, `move_value`, `set_scaled_value` and `move_scaled_value`.
  These are native Rack helpers, including switch snapping and formatted units.

Display scaling follows Rack: linear for base 0, exponential for a positive
base, logarithmic for a negative base, then multiply and offset. Display writes
are immediate, clamp to the range and respect snapping. Switch text lookup is
case-insensitive; unmatched labels or invalid numeric expressions leave the
value unchanged. Out-of-integer-range switch label indices raise `ValueError`
before Rack's float-to-int conversion. `reset()` / `randomize()` are direct
quantity operations: the enable flags govern *module-level* reset/randomize,
not these direct calls.

**Smoothing is not implemented for these unregistered modules.** Setting
`smooth_enabled=True` is metadata only. `set_value` (and its deprecated alias),
`set_min`, `set_max`, `toggle`, `move_value`, `set_scaled_value` and
`move_scaled_value` then raise `OwnershipError` before changing any target.
The helpers must not bypass this guard by indirectly calling native `setValue`.
`set_immediate_value` and display writes remain
available. Rack's smoothing slot retains a raw module pointer, so starting a
smoothing target here could outlive its owner. Full engine-owned smoothing and
lifecycle cleanup remain RP-14B2b/RP-14C/RP-16. No target is queued implicitly,
and no Python callback runs on the audio thread.

```{rackpy-example} module_quantities.py
```

The snippet intentionally uses metadata only so it also runs before Rack's
engine exists. Value/display behavior is tested separately on the live UI
against a native C++ reference in the actual Rack process. These are partial
adapters: full template/class factories, standalone and Python-overridable
quantities, raw identity/vector mutation and JSON (RP-15) remain unimplemented.
Their exact manifest records stay `planned` or `unreviewed`; adapters do not
count as completed callback/template mappings. Mixed value/range virtuals remain
native in production; RP-22 owns explicit laboratory semantics.

## Ownership and failure

The returned handle owns its native module. Component/collection views do not
keep it alive: closing or dropping the last module handle invalidates them.
No pointer is exposed as an integer. `close()` is idempotent; `alive` tells
whether the owned module remains. Reading a closed module or retained component
raises `StaleHandleError`, never a use-after-free.

Calling `close()` during construction raises `OwnershipError` (a callback
cannot delete the module out from under the factory). If the constructor raises,
its original exception propagates and even escaped module/component handles
are invalidated immediately. No half-constructed module can be published.

Construction, configuration, component access and explicit close require
Rack's main thread; Python worker threads get `ThreadError`. The native module
owns only native data, so destruction of a final Python handle on another Python
thread does not run Python callbacks inside the Rack module's destructor.
This is not yet the Rack-owned model-transfer/deletion path (RP-14C/RP-16).

## Bypass configuration

`config_bypass(input_id, output_id)` validates both port IDs and refuses a
second route to the same output with `ValueError`. An input may feed several
outputs. Rack's native `processBypass` preserves the routed channel count and
voltages; the test-hooks build compares it against a standalone C++ reference
for 0, 1 and 16 channels. It is not yet a public Python callback mapping.

`Module.BypassRoute()` is an independent value with mutable `input_id` and
`output_id`, initially -1. `module.bypass_routes` returns a tuple of copied
route values. Changing those copies cannot corrupt native routing. Since this
does not map arbitrary mutation of the public C++ `bypassRoutes` vector, that
field stays `planned` for RP-14C. Never confuse a snapshot with a write-through
route editor.

## Remaining proof gates

The host tests construct and delete these owned native modules **inside actual
Rack**, both at plugin startup and on the UI thread with a live engine. They
exercise 1,000 construction/deletion cycles, retained views, failed and nested
constructors, wrong-thread access, checked indices and native differential
fixtures. Metadata tests additionally exercise 1,000 replacements, 1,000 owner
collections, slot-specific invalidation, null slots, backreference identity and
native-reference names/descriptions/IDs/types. The native test injects a
preparation exception to verify replacement rollback. Quantity tests also cover
1,000 slot replacements and 1,000 owner collections, stale getters/setters,
thread/context rejection, validation rollback, native display/label/value
comparisons, reset/randomize and unsafe smoothing rejection. Borrowed-helper
and label tests add native differential helper replay and edited label lookup,
mutable alias/snapshot behavior, 1,000 alias replacements and owner collections,
conversion failure atomicity, degenerate scaling and indirect smoothing refusal.
The bypass dispatch hook
is test-only and runs off the engine.

Rack model registration, browser previews, native lifecycle/reset/randomize/
sample-rate adapters, save/reload, engine ownership transfer and instrumented
production audio-path gates are **not** established by those tests. They remain
on RP-14B/C and the later tasks. macOS and Windows have not been run.
