# Module construction and components

**Implemented slices: RP-14A, built-in port/light metadata adapters (RP-14B1),
checked built-in quantities (RP-14B2a), and borrowed quantity helpers / mutable
switch-label views (RP-14B2b1), standalone metadata backreferences (RP-14B2b2),
unattached and attached standalone quantities (RP-14B2b3/b4), and port/light
and parameter-quantity class factories (RP-14B2b5/b6), and constructor-scoped
raw module vectors (RP-14B2b7), module identity (RP-14C1) and engine ownership
with engine smoothing (RP-14C2), Linux x86_64 inside Rack Free 2.6.6.** This is
not a registerable instrument yet: nothing public hands a module to Rack's
engine before RP-16. Native lifecycle event adapters (the rest of RP-14C),
plugin/model factories (RP-16), DSP (RP-17), the Python callback
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
`Module.create()` without a callback returns a sealed, empty module. Rack's
deprecated counting constructor `Module(numParams, numInputs, numOutputs,
numLights)` is the overload `Module.create(num_params, num_inputs,
num_outputs, num_lights=0, configure=None)`: it emits `DeprecationWarning`,
calls `config` first and then the optional callback, in the same transaction
(so the callback cannot call `config` again).

Here is a runnable construction-only example. The documentation snippet tests
run this exact file inside Rack:

```{rackpy-example} module_construction.py
```

This does not add the module to Rack's engine or install a DSP callback. A Python
subclass or an attribute named `process` is **not** a DSP implementation.
There is no Module virtual trampoline in this slice. The construction callback
is never stored in the native module, and no audio path invokes it.

## Identity and bypass state

`module.id` is Rack's engine ID: `-1` until an engine assigns one (Rack keeps
IDs below 2^53 so they survive JSON, but any 64-bit integer is accepted, as in
C++). It can be assigned only inside the constructor callback; `get_id()` is
Rack's getter. `is_bypassed()` is Rack's query, and is `False` until Rack's
engine owns and bypasses the module ([engine ownership](#engine-ownership);
nothing public hands a module to the engine before RP-16). Both need the main
thread. The `model` field and `get_model()` wait for model
handles (RP-16). Engine ownership, its deletion path and lifecycle events are
the rest of RP-14C.

## Component access

`get_num_params/inputs/outputs/lights()` and
`get_param/input/output/light(index)` call Rack's corresponding getters.
Getter IDs must satisfy `0 <= index < count`; invalid IDs raise `IndexError`
instead of accessing an unchecked C++ vector.

`params`, `inputs`, `outputs` and `lights` are checked views of Rack's
vectors. They support length, iteration and integer indexing (including
Python's negative indices); slicing and named lookup are not supported. Setting
a component's value writes through to the actual Rack component. Repeated
lookups of a live component preserve Python identity. Iteration snapshots the
component *handles*, not their changing values. During construction they also
take Rack's raw vector edits ([raw module vectors](#raw-module-vectors)).
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
`output_infos` and `light_infos` are `Components` collections with negative
indexing and iteration; during construction they take
[raw vector edits](#raw-module-vectors).

The adapters expose mutable `name` and `description`, `get_name()` and
`get_description()`. A port also has `get_full_name()` (Rack appends " input"
or " output"), `type` and `port_id`; a light has `light_id`. `module` returns
the owning Python handle (or `None`). These identity fields can be assigned
only during construction, and `module` only the owning module or `None`;
the seal checks that they match the slot.
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

Borrowed adapters are distinct from standalone, subclassable `PortInfo` /
`LightInfo`. To put one of those classes into a slot, pass it as `info_type`
([metadata class factories](#metadata-class-factories)) or assign it to the
slot ([raw module vectors](#raw-module-vectors)). Standalone metadata
backreferences are documented in
[Engine components](engine-components.md#portinfo-and-lightinfo).

## Metadata class factories

Rack's `configInput<TPortInfo>`, `configOutput<TPortInfo>` and
`configLight<TLightInfo>` construct a metadata object of the given class and
put it in the slot. In Python, pass the class as `info_type`:
`config_input(port_id, name="", info_type=None)`, and the same for
`config_output` and `config_light`. `info_type` must be `PortInfo` (or
`LightInfo` for lights) or a Python subclass; anything else raises `TypeError`.
Without `info_type` the built-in view described above is used.

RackPy calls `info_type()` with no arguments, on the main thread inside the
constructor callback, as Rack's `new TPortInfo` would. Your `__init__` may set
any field; then RackPy sets `module`, `type`, `port_id` (or `light_id`) and
`name` exactly as Rack's template does, so a description set in `__init__`
survives. The call returns that same Python object, and `get_input_info`,
`input_infos` and the other getters return it again: the identity is preserved,
with its class and attributes. An override of `get_name` or `get_description`
is what Rack's UI shows. When Rack calls it without Python holding the GIL, a
raised exception is contained and Rack's own answer is used instead.

```{rackpy-example} metadata_factories.py
```

**Ownership.** The slot owns the object from then on and keeps its Python
half alive. Its fields and methods are main-thread only, like a built-in view
(`ThreadError` elsewhere); a standalone object keeps its any-thread fields.
When the slot replaces it, or the module is closed, collected or its
constructor fails, the object goes back to Python, `alive` becomes `False` and
every field or method raises `StaleHandleError`, like a pointer Rack deleted.
The Python object itself lives as long as you reference it. It may refer back
to its module: a slot keeps its object alive, but the module's handle reports
that edge to Python's garbage collector (RP-15A), so the cycle is collected
once nothing else refers to it.

**Errors are atomic.** The constructor runs before anything changes: if it
raises, or returns something other than a new instance of exactly `info_type`
(a `__new__` that returns another object), the slot keeps its old metadata.
An object a slot already owns raises `OwnershipError`; one a slot has released
raises `StaleHandleError`. The constructor may reenter the module, for example
to configure the same slot; RackPy checks the construction phase and the ID
again afterwards, and the outer call's object then replaces the inner one.

These six records (the three templates and `get_input_info`,
`get_output_info`, `get_light_info`) are `implemented`. Parameter quantities
have their own class factories, described next.

## Quantity class factories

Rack's `configParam<TParamQuantity>`, `configSwitch<TSwitchQuantity>` and
`configButton<TSwitchQuantity>` work the same way: pass the class as
`quantity_type`. `config_param` takes `ParamQuantity` or any subclass,
including `SwitchQuantity`; `config_switch` and `config_button` need
`SwitchQuantity` or a subclass. RackPy checks Rack's arguments (IDs, bounds,
display coefficients) **before** it calls `quantity_type()`, then sets exactly
the fields Rack's template sets: `module`, `param_id`, the bounds and default,
`name`, `unit` and the display coefficients; `config_switch` also enables
snapping, disables smoothing and replaces `labels`; `config_button` enables
snapping and disables smoothing and randomization, and leaves `labels` as your
constructor set them. Anything else your `__init__` sets (a description,
`display_precision`, `randomize_enabled`) survives. Finally the Param takes the
quantity's default value. The call returns your object, and
`get_param_quantity` and `param_quantities` return it again.

```{rackpy-example} quantity_factories.py
```

The ownership, retirement, error and reentrancy rules are those of
[metadata class factories](#metadata-class-factories): main thread only, `alive`
becomes `False` and every member raises `StaleHandleError` once the slot
releases the object, including a retained `labels` alias. Calls from Python
keep the rules of [attached quantities](#standalone-quantities):
value and display methods need Rack's live bound UI context, and smoothing
writes are refused until Rack's engine owns the module
([engine ownership](#engine-ownership)).

**Calls from Rack.** A slot quantity is the one place in this API that Rack
itself may call from another thread: Core's MIDI-Map reads and writes mapped
parameters from the engine thread. Those calls arrive without the GIL. They
get Rack's own native behaviour for the mixed value/range methods, never a
Python call and never an exception into Rack. A UI-safe override that Rack
calls on the main thread runs through RackPy's contained dispatch: if it
raises, Rack's own answer is used. Mixed value/range overrides are refused
when the class is defined, as for every Quantity; RP-22 owns their laboratory
semantics. RackPy modules are not yet registered with Rack's engine (RP-16),
so in this release only the test hooks make such calls.

Retiring an object hands it back to Python and may run its finalizer. An
override that closes its own module leaves its object stale: the rest of that
call raises `StaleHandleError`. Rack's C++ API gives no protection against a
callback deleting its own module, so do not do that in a callback Rack makes.

The three templates and `get_param_quantity` are `implemented`, and so is
the raw `paramQuantities` vector ([raw module vectors](#raw-module-vectors)).
For the ParamQuantity and SwitchQuantity records themselves, see
[what stays open](#quantity-records-that-stay-open).

(standalone-quantities)=
## Standalone quantities and weak module attachment

`rackpy.engine.ParamQuantity` and `SwitchQuantity` are aliases of the corresponding
`rackpy.raw.engine` classes. They construct real, Python-owned SDK objects and
inherit `rackpy.raw.Quantity`; a SwitchQuantity also inherits ParamQuantity.
Constructing one does not install it in a `Module` slot; pass its class to a
[quantity class factory](#quantity-class-factories) for that. Their mutable
`module` field is a weak backreference: assigning a live RackPy `Module` preserves
identity but neither retains that module nor transfers quantity ownership. Assign
`None` to detach. When the module closes, construction rolls back or becomes
unreachable, RackPy clears the native pointer and the quantity reads `None`.
`get_param()` follows Rack's raw semantics and returns the matching borrowed Param,
or `None` for a null module or invalid `param_id`.

Attached value/display/helper/reset/randomize operations require a live bound Rack
UI context and a valid parameter ID; invalid IDs are rejected before Rack's
unchecked vector access. Unattached values retain Rack's null-module no-op/zero
behavior. Smoothing writes to an attached quantity are refused unless Rack's
engine owns its module ([engine ownership](#engine-ownership)); use
`set_immediate_value()` where the immediate semantics are intended.

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
explicit laboratory semantics. A standalone object is UI-only; only a class
factory installs one in a Module slot.

All native access, construction and field operations require Rack's main thread.
Assigning a closed module or a non-Module value fails without changing an existing
backreference. Closing or collecting a module clears the native pointer without
keeping Python work on its teardown path; a quantity does not keep the module
alive. Python code in an override is ordinary user code, not a sandbox or an audio
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
Each also accepts `quantity_type` ([quantity class factories](#quantity-class-factories));
without it, they configure the built-ins described here.
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
for initial configuration. `param_id` and `module` can be assigned only during
construction (`module` only the owner or `None`). `get_param()` returns the
same checked Param as the owning module, or `None` for an ID out of range. `alive` remains a
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

**Smoothing needs Rack's engine to own the module.** Rack's smoothing slot
keeps a raw module pointer, which Rack clears only when it removes the module
from its engine, so for a module Python owns, `smooth_enabled=True` is metadata
only: `set_value` (and its deprecated alias), `set_min`, `set_max`, `toggle`,
`move_value`, `set_scaled_value` and `move_scaled_value` then raise
`OwnershipError` before changing any target. The helpers cannot bypass this
guard by indirectly calling native `setValue`. `set_immediate_value` and
display writes remain available. Once Rack's engine owns the module, the same
calls smooth as in Rack ([engine ownership](#engine-ownership)). No target is
queued implicitly, and no Python callback runs on the audio thread.

```{rackpy-example} module_quantities.py
```

The snippet intentionally uses metadata only so it also runs before Rack's
engine exists. Value/display behavior is tested separately on the live UI
against a native C++ reference in the actual Rack process. Built-in views have
no Python virtuals; for Python overrides, configure the slot with a
[quantity class factory](#quantity-class-factories).

### Quantity records that stay open

The ParamQuantity and SwitchQuantity fields, UI virtuals (`get_label`,
`get_unit`, the display methods, `get_description`), `reset`, `randomize`,
`get_immediate_value`, `set_immediate_value`, `get_param` and both classes are
`implemented` (RP-14B2b8): Python objects, slot-owned objects and built-in
views give them their full semantics, including Python overrides of the UI
virtuals. Five records stay `planned`, with their responsible task:

- `get_value`, `set_value`, `get_min_value`, `get_max_value` and
  `get_default_value` are *mixed*: Rack calls them from the engine thread as
  well. Their native production path is complete, and a subclass that
  overrides one is refused when it is defined. A Python override needs the
  non-real-time callback laboratory (RP-22), which the coverage contract
  requires for these methods.
- JSON (`to_json`, `from_json`) is RP-15.

Smoothing, including the deprecated `set_smooth_value`/`get_smooth_value`,
works once Rack's engine owns the module (RP-14C2) and is refused before.

## Raw module vectors

Rack's `Module` has eight public vectors: `params`, `inputs`, `outputs`,
`lights`, and the owning pointer vectors `paramQuantities`, `inputInfos`,
`outputInfos` and `lightInfos`. A plugin's C++ constructor may edit them after
`config`. RackPy maps that **inside the constructor callback**, after `config`:
item assignment, `del`, `insert`, `append`, `clear` and `resize(n)` on the
matching collection. Negative indices count from the end, `insert` clamps its
position like `list.insert`, and other out-of-range indices raise `IndexError`.

- **Component vectors** hold values. Assigning a `Param`, `Input`, `Output` or
  `Light` copies it into the slot, like C++ assignment; the source stays
  independent. `resize` adds default components. A structural edit (anything
  but item assignment) invalidates every view of *that* vector: Rack's vector
  may have moved its storage, as with C++ iterators. Views of the other vectors
  stay valid.
- **Metadata vectors** hold owning pointers. Assign a `ParamQuantity` (or
  `SwitchQuantity`), `PortInfo` or `LightInfo` you created, or `None` for a null
  slot. The module takes ownership, exactly as `inputInfos[i] = info` does in
  C++: no field is changed, so set `module`, the ID and the port type yourself.
  `resize` adds null slots. Built-in views, objects another slot owns and
  objects a slot has released are refused.
- **Removal releases.** When a raw edit removes a pointer (assignment over it,
  `del`, `clear`, shrinking), Rack would not delete the object. A Python object
  comes back to you, still usable; if its `module` points at this module the
  link is tracked, so deleting the module later sets it to `None`. Built-in
  metadata has no other owner, so it is deleted and its views become stale.
  Replacement by a `config_*` helper and module deletion still delete, as in Rack.

```{rackpy-example} raw_vectors.py
```

**The seal checks what Rack relies on.** When the constructor callback
returns, Rack's own code (saving, reset, randomize, tooltips, the cable menu)
assumes invariants it never checks. RackPy checks them and fails the
construction with `ValueError` otherwise: each component vector matches its
metadata vector in size; every parameter has a quantity and every port has
metadata (light slots may be `None`); every object's `module` is this module and
its ID (and port type) matches its slot; every bypass route is inside the port
vectors.

**After the seal** only item assignment to the component vectors remains:
slot assignment and structural edits raise `OwnershipError`, and so do writes
to the identity fields (`module`, the IDs and the port type) of any metadata a
slot owns. Rack's engine and UI index with those values unchecked.
Every edit requires the main thread, and none is allowed before `config`.
These eight records are `implemented` with this contract; `bypassRoutes` is
RP-14C.

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
Rack's main thread; Python worker threads get `ThreadError`. Dropping a final
Python handle on another Python thread destroys the module there, with the GIL.
Without class-factory metadata that runs no Python. Transferred metadata goes
back to its Python object on that thread, so a finalizer of yours may run there.
A module Rack's engine owns is deleted by Rack instead; see below.

## Engine ownership

In Rack, a module belongs to the engine once Rack adds it: Rack removes it
from the engine and deletes it when its module widget goes, or at shutdown.
RackPy implements Rack's side of that (RP-14C2), which RP-16's model factory
will use; until then only the test hooks hand a module to the engine.

- Only a sealed module can go to the engine. Its `id` is then the one the
  engine assigned, and every handle and view stays valid, with the same
  identity.
- `close()` raises `OwnershipError`: Rack deletes it. Dropping every Python
  handle does not delete it either, and Rack can hand it out again (as a
  module widget does), giving the same kind of handle.
- When Rack deletes it, on its main thread without the GIL and after removing
  it from the engine, every handle, view and collection raises
  `StaleHandleError` (`alive` is `False`), and transferred metadata goes back
  to Python retired, taking the GIL for that alone. `close()` is then a no-op.
- Smoothing writes are allowed, and smooth as in Rack: `get_value` returns the
  target at once and the engine moves the Param towards it.

## Module JSON

Rack's serialization methods are bound with Rack's behaviour (RP-15A):
`to_json()`/`from_json(data)` (the whole module; they need a registered model,
RP-16), `params_to_json()`/`params_from_json(data)` (Rack's live bound UI
context), and `data_to_json()`/`data_from_json(data)`, which reach the
handlers set in the constructor callback with
`set_data_handlers(to_json=..., from_json=...)`. A `ParamQuantity` subclass may
override `to_json`/`from_json`; Rack's `paramsToJson` reaches the override.
Values cross as ordinary Python data under the runtime's JSON policy, never as
a shared `json_t`. A module's handle also reports its handlers and its slots'
transferred objects to Python's garbage collector, so closures and subclass
attributes that refer back to the module do not leak. Rules, threads and error
handling are explained in [save and restore a module's state](../how-to/state.md).
Patch storage directories (`getPatchStorageDirectory`,
`createPatchStorageDirectory`) are later RP-15 work.

## Lifecycle events

Rack tells a module about its life through events: `onAdd` and `onRemove`
(engine membership), `onBypass`/`onUnBypass`, `onPortChange` (a cable connected
or disconnected), `onReset` and `onRandomize` (user commands),
`onSampleRateChange`, `onSave`, and `onSetMaster`/`onUnsetMaster` (which
module drives the engine). RackPy's modules handle all eleven natively
(RP-14C3): Rack's own behaviour runs first (resetting or randomizing the
parameters through their quantities, and calling Rack's deprecated
no-argument events), then RackPy records the event. No Python runs in these
handlers apart from what Rack's behaviour itself calls, such as a quantity
subclass's `reset` override, and that Python cannot take Rack's engine lock
(`EngineLockError`): the engine is locked exclusively for every event except
`onSave`.

```{rackpy-example} lifecycle_events.py
```

**Observing events.** `module.lifecycle_events()` returns the events since
the last call, oldest first, as `(sequence, event)` pairs, where `event` is a
copy of Rack's own record: `Module.AddEvent`, `Module.PortChangeEvent` (with
`connecting`, `type` and `port_id`), `Module.SampleRateChangeEvent` (with
`sample_rate` and `sample_time`) and so on. This is RackPy's API, not Rack's:
Rack may dispatch the sample-rate and master events on an audio driver thread,
so recording takes no lock, allocates nothing and never calls Python, and
Python reads the records later on the main thread. The queue holds 64 events;
when it is full, a new event is dropped and counted in
`dropped_lifecycle_events`. Sequence numbers count dropped events too, so a gap
shows a loss.

**Calling a handler.** `module.on_reset(Module.ResetEvent())` and its
siblings call the module's handler directly, as C++ plugin code may call
`Module::onReset(e)`. The module must be sealed and Python-owned, and Rack's
live bound UI context is required, because Rack's behaviour writes parameters
through the engine; the handler then runs exactly as when Rack dispatches it.
For a module Rack's engine owns, Rack dispatches the events, under its engine
lock, and direct calls raise `OwnershipError`. The deprecated no-argument forms
(`on_reset()`, `on_add()`, `on_remove()`, `on_randomize()`,
`on_sample_rate_change()`) warn and call Rack's empty deprecated handler,
which records nothing.

Python cannot override these handlers yet: RackPy modules are not Python
subclasses. Observing snapshots is not the same semantics as a synchronous
Python handler, so the `on*` records stay `planned`: Python module classes
(RP-16) are to bring overrides of the UI-thread events (everything but the
sample-rate and master events, which Rack may run on an audio driver thread),
and the non-real-time callback laboratory (RP-22) synchronous Python for those
driver events. The event records themselves are `implemented`.

## Bypass configuration

`config_bypass(input_id, output_id)` validates both port IDs and refuses a
second route to the same output with `ValueError`. An input may feed several
outputs. Rack's native `processBypass` preserves the routed channel count and
voltages; the test-hooks build compares it against a standalone C++ reference
for 0, 1 and 16 channels. It is not yet a public Python callback mapping.

`Module.BypassRoute()` is an independent value with mutable `input_id` and
`output_id`, initially -1. `module.bypass_routes` is a checked collection of
Rack's route vector whose items are **copies**: changing a copy cannot corrupt
routing. Inside the constructor callback, after `config`, it takes Rack's raw
vector edits like the other [raw module vectors](#raw-module-vectors): item
assignment of a `BypassRoute`, `del`, `insert`, `append`, `clear` and
`resize` (new routes are `(-1, -1)`). A raw edit can create a second route to
the same output, as C++ can; `config_bypass` still refuses that, as Rack's
`configBypass` asserts. The seal rejects routes outside the port vectors, and
after it route edits raise `OwnershipError`, because Rack's engine reads the
routes while it processes a bypassed module.

When Rack's engine owns the module and bypasses it, Rack calls
`processBypass` instead of `process`: each route copies its input's channels
and voltages to its output. The live-UI tests drive this through cables in
the real engine with 0, 1 and 16 channels and compare the output with Rack's
own `processBypass` on a plain module (RP-14C4). Enabling the module again
clears its outputs, as Rack does.

## Remaining proof gates

The host tests construct and delete these owned native modules **inside actual
Rack**, both at plugin startup and on the UI thread with a live engine. They
exercise 1,000 construction/deletion cycles, retained views, failed and nested
constructors, wrong-thread access, checked indices and native differential
fixtures. Metadata tests additionally exercise 1,000 replacements, 1,000 owner
collections, slot-specific invalidation, null slots, backreference identity and
native-reference names/descriptions/IDs/types. Class-factory tests replay
native `configInput<CustomInfo>` and `configParam/Switch/Button<Custom>`
fixtures, including replacement, read slot quantities from a Rack thread
without the GIL, and cover
1,000 replacements and owner collections with the transferred-object count
back to its baseline, rollback, reentrant constructors, wrong classes and
threads, and Rack-side overrides without the GIL. The native test injects a
preparation exception to verify replacement rollback. Quantity tests also cover
1,000 slot replacements and 1,000 owner collections, stale getters/setters,
thread/context rejection, validation rollback, native display/label/value
comparisons, reset/randomize and unsafe smoothing rejection. Borrowed-helper
and label tests add native differential helper replay and edited label lookup,
mutable alias/snapshot behavior, 1,000 alias replacements and owner collections,
conversion failure atomicity, degenerate scaling and indirect smoothing refusal.
The offline bypass dispatch hook is test-only.

With the test hooks playing Rack's part, the live-UI suite also has Rack's
engine own modules: adoption and Rack-side deletion (1,000 times), engine
smoothing, every lifecycle event Rack dispatches (add, remove, bypass, sample
rate, master, save, cable port changes, and a seeded reset and randomize equal
to a plain Rack module's), a raising override contained inside an event,
engine-lock refusal inside exclusive events, bypass routing through cables
with 0, 1 and 16 channels, and Rack's own save and reload of a configured
module, which restores parameter values and bypass while the constructor
rebuilds scales, labels and routes. The lifecycle adapters' object references
no CPython (`bin/rackpy-dev audio-symbols`).

Not established: Rack model registration and browser previews (a module
widget's `module` is `None` there; RP-16), Python overrides of the lifecycle
events (RP-16, RP-22), Python JSON (RP-15), and an audio driver thread actually
dispatching sample-rate or master events: the tests use no audio device, so
that path is known from Rack's source and the Python-free object only. macOS
and Windows have not been run.
