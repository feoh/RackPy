# Module construction and components

**Implemented slices: RP-14A and built-in port/light metadata adapters (RP-14B1),
Linux x86_64 inside Rack Free 2.6.6.** This is not a registerable instrument yet.
Parameter metadata and full metadata class factories (RP-14B2), native lifecycle
adapters (RP-14C), plugin/model factories (RP-16), DSP (RP-17), the Python callback
laboratory (RP-22), and expanders (RP-23) remain planned. See the
[coverage report](coverage.md) for declaration-level status.

## Construction transaction

`rackpy.engine.Module` and `rackpy.raw.engine.Module` are the same native
binding. `Module.create(configure=None)` constructs an owned, empty Rack module.
When supplied, `configure(module)` runs synchronously on Rack's main thread.
Its return value is ignored; the factory returns the same Python handle it
passed to the callback. Only that callback may call `config`, `config_bypass`,
`config_input`, `config_output` and `config_light`. These are construction
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
identity-field/module-pointer mutation, raw vector mutation and standalone
metadata `module` fields are also absent. RP-14B2 retains these obligations,
along with ParamQuantity/SwitchQuantity and their mixed-thread virtuals.
Accordingly the template, getter and raw-field manifest records remain
`planned`: built-in adapters alone are not complete public semantics. No
production callback dispatches to Python in this slice.

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
preparation exception to verify replacement rollback. The bypass dispatch hook
is test-only and runs off the engine.

Rack model registration, browser previews, native lifecycle/reset/randomize/
sample-rate adapters, save/reload, engine ownership transfer and instrumented
production audio-path gates are **not** established by those tests. They remain
on RP-14B/C and the later tasks. macOS and Windows have not been run.
