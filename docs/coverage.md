# API coverage contract

## Establish a denominator before writing bindings

The denominator is the SDK inventory,
[`upstream/api-surface.json`](../upstream/api-surface.json) (RP-05). It is generated
by `bin/inventory_sdk.py`, described in the [surface schema](reference/surface-schema.md)
and counted in the [surface summary](reference/surface-summary.md). The tool parses
a translation unit including only `rack.hpp` with the SDK's defines, dependency
headers and compiler flags, and combines AST data with preprocessor records. It
uses regular expressions only for code the preprocessor skipped. So far only
Linux x64 has been extracted; macOS and Windows are recorded gaps.

Keep stable IDs from qualified C++ name, kind, canonical signature, template
parameters, source provenance and conditional availability. Enumerate inherited
public behavior without multiplying aliases into a misleading success count.
Record source revision, SDK checksums, tool versions and command lines.

## Pinned surface (RP-02)

RP-02 fixed the inputs and the definition of the surface. **It did not count the
surface.** No coverage total exists until RP-05 produces the inventory.
Baseline, digests and toolchain facts are in [compatibility](compatibility.md).

**Definition.** The surface is every declaration that a translation unit
containing only `#include <rack.hpp>` can see in a Rack-owned header, on each
advertised platform, at Rack commit `061ccf63c1758599396ac1bb10d47345d9d34076`.
That includes:

- declarations in Rack headers that are reachable only transitively, such as
  `history.hpp`, `weakptr.hpp`, `Quantity.hpp`, `color.hpp`, `app/common.hpp`,
  `ui/common.hpp` and the `simd/sse_mathfun*.h` headers;
- the global-namespace C ABI hooks in `plugin/callbacks.hpp` (`init`,
  `destroy`, `settingsToJson`, `settingsFromJson`). Plugins implement them,
  so RackPy's host provides them. They are entries even though they are not in
  `namespace rack`;
- macros defined in Rack headers, and declarations on any branch of a platform
  conditional.

The Linux x64 closure reaches **112 Rack headers** and **13 dependency headers**.
Each is listed with its SHA-256 in
[tests/fixtures/sdk-provenance.json](../tests/fixtures/sdk-provenance.json) and
checked by `bin/rack-sdk closure`. Every reached header maps to a family in
[scope](scope.md#coverage-families):

| Family | Reached headers |
| --- | --- |
| Entry point and namespace imports | `rack.hpp` |
| 1. Common/value utilities | `arch`, `common`, `math`, `color`, `string`, `random`, `logger`, `Quantity`, `mutex`, `weakptr` |
| 2. Engine | all 10 `engine/*` headers |
| 3. Plugin/model/assets/helpers | `plugin`, `plugin/Plugin`, `plugin/Model`, `plugin/callbacks`, `asset`, `helpers` |
| 4. Widgets, UI, app, component library | 57 headers in `widget/`, `ui/`, `app/`, plus `componentlibrary` |
| 5. Window/rendering/context | `window/Window`, `window/Svg`, `context` |
| 6. DSP and SIMD | 14 `dsp/*` and 5 `simd/*` headers |
| 7. Audio/MIDI | `audio`, `midi` |
| 8. Services | `settings`, `system`, `network` (synchronization is in `mutex`, family 1) |
| 9. History | `history`, reached through `app/ModuleWidget.hpp` and `app/RackWidget.hpp` |

**Shipped but unreachable.** These SDK headers are not reached from `rack.hpp`.
`rack.hpp` says that including other Rack headers directly is unsupported, so
they are outside the surface. They are recorded so that a later Rack version
cannot pull one in unnoticed:

| Header | What it is | Disposition |
| --- | --- | --- |
| `patch.hpp` | Patch manager definition | Outside. `Context::patch` stays an opaque handle (policy 4) |
| `midiloopback.hpp` | Loopback MIDI context | Outside. `Context::midiLoopbackContext` stays opaque |
| `library.hpp`, `tag.hpp` | VCV Library client, tag tables | Outside |
| `keyboard.hpp`, `gamepad.hpp`, `rtmidi.hpp`, `rtaudio.hpp` | Built-in driver implementations | Outside. Drivers are reachable only through `audio::`/`midi::` interfaces |
| `app/Browser.hpp`, `app/MenuBar.hpp`, `app/TipWindow.hpp` | Rack's own application windows | Outside |
| `rack0.hpp` | v0 migration shim of deprecated aliases. It includes `rack.hpp`, not the reverse | Outside. **Owner review item:** confirm, or add it as an opt-in compatibility family |

**Dependency headers reached:** `jansson.h`, `jansson_config.h`, `nanovg.h`,
`nanovg_gl.h`, `nanovg_gl_utils.h`, `nanosvg.h`, `blendish.h`,
`GLFW/glfw3.h`, `GL/glew.h`, `pffft.h` and three `speex/` headers. Their own
APIs are not in the denominator. Policy 13 covers their role in Rack signatures.

**Preliminary survey.** `bin/rack-sdk survey` records lists for RP-05 to
cross-check. Its counts are navigation data, not inventory:
69 PRIVATE declarations, 39 attribute-deprecated declarations, 23 doc comments
that mention deprecation, 6 "Unstable API" notes, 24 incomplete Rack record types
and 137 template declarations. The RP-05 inventory agrees on the PRIVATE,
attribute-deprecated and incomplete-record counts (a test checks this). It also
found 7 comment-only deprecations that the first version of the survey missed
because it matched only uppercase `DEPRECATED`. The survey now matches
`Deprecated` too. The inventory additionally treats the 34 aliases in the
deprecated `rack::event` namespace as deprecated.

Two inventory hazards turned up while building the survey:

- Clang nests the imported record inside the `UsingShadowDecl` for
  `using plugin::Model;`. A naive walker therefore reports a phantom
  `rack::Model` declaration.
- `using std::fabs;` in `rack::simd` imports libstdc++ overloads, including a
  deprecated `std::complex` overload. Imports must not count as Rack
  declarations, and their deprecation is not Rack's.

## Completeness policy

These rules decide what counts toward complete coverage. They apply to every
family task. A rule may only be relaxed with owner review, recorded in Witan and
in this file.

1. **Scope.** Exactly the [pinned surface](#pinned-surface-rp-02). A difficult
   public declaration is not an internal one.
2. **PRIVATE.** Declarations carrying `PRIVATE` are `excluded_internal`; the
   attribute is the evidence. They are never callable, including from
   `rackpy.raw`. `rack.hpp` makes `PRIVATE` a deprecation under clang and an
   `error` attribute under GCC, so the inventory must detect it from the AST and
   not rely on a failing build.
3. **Pimpl internals.** A public `Internal* internal` member whose type is an
   incomplete `Internal` struct is `excluded_internal`, with the incomplete type
   as evidence. The inventory finds 21 such types and 21 such members. They are
   never exposed.
4. **Opaque forward declarations.** `patch::Manager` and
   `midiloopback::Context` are declared but never defined in the closure. The
   public fields that hold them are entries. They are mapped as opaque,
   context-bound handles that support identity and null checks only. Nobody
   guesses their definitions. Operations on them are not supported API.
5. **Deprecation and instability.** Attribute-deprecated and comment-only
   deprecated declarations are both mapped and emit `DeprecationWarning`. The
   inventory records both kinds. Members documented as "Unstable API"
   (`Param::value`, `Light::value`, `Port::voltages`, `Port::value`,
   `Port::channels`, `Port::active`) are mapped and documented as unstable.
6. **Overloads, defaults and aliases.** Each overload is its own entry, and
   Python dispatch must reach each one. Default arguments are recorded and
   honoured. A typedef or alias such as `SVGPanel` is an alias entry that is
   complete only when its target is. Totals count it once.
7. **Imports.** `using namespace` directives in `rack.hpp` and using-declarations
   such as `using plugin::Model` or `using std::fabs` are import records. They
   determine expected `rackpy.raw` name paths but do not add declarations.
8. **Pointers, references, arrays and unions.** Every pointer or reference
   parameter, return value or field is classified as owning, borrowed,
   transferred, nullable, array or out-parameter, using RP-06 evidence. None is
   exposed as an integer address. Fixed arrays such as `Port::voltages[16]` and
   anonymous unions such as `Port::value`/`voltages` and `channels`/`active` get
   views with explicit aliasing. Mutable reference returns need explicit
   write-through semantics.
9. **Macros.** Each Rack macro gets one of three reviewed dispositions:
   - *constant*, mapped to a Python constant with its per-platform value, e.g.
     `RACK_MOD_CTRL`, `RACK_MOD_*_NAME`, `CHECKMARK_STRING`;
   - *semantic*, mapped to equivalent behaviour, e.g. `APP` to the current
     context accessor, `DEFER` to a context manager, `ENUMS` to an enum
     builder, and `DEBUG`/`INFO`/`WARN`/`FATAL` to logging calls that capture the
     Python caller's location. `LENGTHOF`, `CONCAT`, `TOSTRING`, `BINARY*`,
     `RECT_ARGS`, `VEC_ARGS` and `DECLARE_VECTOR_OPERATOR_*` each need one too;
   - *build configuration*, recorded and mapped where it is observable at run
     time (`ARCH_*` become platform constants). Otherwise it is reviewed as
     having no runtime meaning (`GLEW_STATIC`, `GLEW_NO_GLU`, `NANOVG_GL2`,
     `PRIVATE`, `DEPRECATED`).

   A macro never silently leaves the denominator.
10. **Templates.** These rules replace "bind whatever compiles":
    - Class templates with element-type parameters (`TSchmittTrigger<T = float>`,
      `TBiquadFilter`, `TSlewLimiter`, …) map every instantiation that Rack
      names (`SchmittTrigger`, …) and every element type that the public
      semantics need, at least `float` and `simd::float_4`. Each mapped
      instantiation is an entry.
    - Function templates over widget or module types (`createParam<T>`,
      `createModel<TModule, TModuleWidget>`, menu-item helpers, …) map to
      factories that accept a class, including Python subclasses.
    - Mixin templates such as `TRedLight<TBase>` and `LightButton<TBase, TLight>`
      map to class factories over a base type.
    - The primary template `simd::Vector` is declared but not defined. Only
      the explicit specializations `Vector<float, 4>` and `Vector<int32_t, 4>`
      exist, and they are the mapped set.
    - If a template's parameter space cannot be represented faithfully (for
      example arbitrary `T` and size in `RingBuffer<T, S>`), propose a precise
      generic contract to the owner. The release stays blocked until the owner
      decides ([release bar](#release-bar)).
11. **Virtual methods and callbacks.** Every virtual method is an entry with a
    callback strategy from RP-06: a UI-safe Python override, a native production
    path plus a laboratory Python path, or *unresolved*. An unresolved strategy
    blocks the family task. The C ABI hooks from policy 1 are implemented by the
    RackPy host and forward to Python where RP-06 allows it.
12. **Platforms.** A conditional declaration has per-platform availability, for
    example `string::UTF16toUTF8` on Windows only, or the macOS value of
    `RACK_MOD_CTRL`. Absent on a platform means *not available there*, recorded
    per platform. It is not an exclusion. Coverage is reported for each
    advertised platform. The SSE4.2 and SIMDe implementations of `simd::` must
    behave the same and are tested on both.
13. **External boundary types.** For each Rack declaration whose signature uses
    a dependency type, the necessary semantics are mapped: `json_t` as Python
    values (see [architecture](architecture.md#exceptions-and-state)),
    `NVGcontext` and `NVGcolor` drawing scoped to draw arguments,
    `NSVGimage` through `window::Svg`, `GLFWwindow` in `window::Window` as an
    opaque handle, and GLFW key and modifier constants used by events. `pffft` and `speexdsp` are reached only through Rack's `dsp`
    types. RP-05 lists which external declarations Rack signatures reach. Those
    become boundary-type entries.
14. **Unknowns block.** An entry that cannot be classified stays `unreviewed`
    and blocks a full-coverage release. A failed extraction is an error, never
    an empty report.

## The binding manifest (RP-11)

The per-symbol records below live in
[bindings/manifest.json](../bindings/manifest.json), one per counted inventory
entry. `bin/check_coverage.py check` enforces this page's policies against the
inventory, the reviewed exclusion allowlist, the callback and engine-lock
tables, the cited tests and docs, and an introspection of the built package;
`bin/generate_bindings.py` derives the registration scaffold, stubs and the
[coverage report](reference/coverage.md) from it. The design is
[ADR 008](decisions/008-binding-manifest.md); the workflow is
[Adding bindings](contributing-bindings.md).

## Per-symbol record

Each entry must have:

- Upstream ID, header/line, namespace, declaration kind, availability and deprecation.
- Public/internal decision with evidence; PRIVATE is never callable.
- Python raw and facade paths; overload and enum/property conversions.
- Ownership, nullability, thread affinity, mutability and callback strategy.
- Macro/template mapping and supported generic constraints or specializations.
- Implementation path, behavioral test node IDs and documentation anchors.
- Status: `unreviewed`, `planned`, `implemented`, `verified`,
  `unrepresentable`, or `excluded_internal`, with reason and reviewer.
- Gate ownership for anything platform-conditional or not yet representable.

A “native only” implementation is not a verified Python mapping. A method that
raises NotImplementedError is not implemented. Aliases do not compensate for
missing overloads. Safety-restricted calls must document where their semantics
can actually be exercised.

## Three independent checks

1. **Declaration completeness:** inventory diff catches new, removed, changed or
   conditionally hidden upstream declarations. Unknowns fail.
2. **Reachability and behavior:** import/introspect the shipped module; prove
   signatures and every overload's semantics using native differential fixtures
   and actual-host integration where required.
3. **Reader usability:** mapping table links to docs; every binding has a human
   explanation of ownership/thread restrictions, and examples compile/run.

The coverage report is broken down by family and platform. Header-level checkmarks
are useful navigation, never evidence of complete bindings.

## Release bar

A full-surface release requires zero public unreviewed/planned/unimplemented/
unrepresentable entries on each advertised platform, no unexplained exclusions,
and verified callback, macro and template mappings. Purely internal declarations
may be excluded only with source evidence and review. Public deprecated APIs
remain mapped and warned about until the compatibility policy explicitly changes.

If an infinite C++ template space cannot have a faithful Python generic/extension
mapping, propose a precise contract to the owner; do not silently trim scope.
The release task remains blocked while that contract is unresolved.

Early **preview** releases may be partial only if they say so and link to all gaps.
Changing the target SDK requires an inventory diff, compatibility work, sample
reruns and owner-visible release notes, not a tag edit.
