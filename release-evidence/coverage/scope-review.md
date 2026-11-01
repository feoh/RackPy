# Scope review of the binding manifest (RP-42, step 3)

RP-42 asks for a review of template policies, macros, deprecated declarations, virtual overrides, platform
branches and external-type adapters, specifically for scope cheating: a record counted as implemented whose
public semantics Python still cannot reach. This is the implementer's own review, done on 2026-11-01 against
`bindings/manifest.json` at the RP-42 branch. It is **not** the independent review that step 6 requires, and it
promotes nothing to `verified`.

## Method

Mechanical passes over the manifest and the inventory (`upstream/api-surface.json`), then reading what they
surfaced:

1. Every record by declaration kind, status and Python mapping kind.
2. Every implemented record whose notes admit a restriction (refused, not exposed, unreachable, omitted,
   unsupported, not offered, ignored and similar): 210 records, 136 distinct sentences, each read.
3. Every template, alias template and specialization record (144), for its instantiation policy.
4. Every exclusion, against the inventory's classification.
5. Every virtual declaration in the inventory (437), for a callback strategy.
6. Every deprecated declaration (39), every macro (44) and every record with no runtime meaning (17).
7. Native bindings that always throw (a stand-in): `grep` over `native/bindings` for refusal messages.

## Findings, all fixed in this branch

| Finding | Before | Now |
| --- | --- | --- |
| `rack::string::f` and its template refused `*` widths and precisions | A C++ caller can write `string::f("%*d", w, x)`; from Python that part of printf was unreachable, while the record counted as implemented | A `*` takes an `int` argument first, as printf does, and goes to Rack's `string::f` with the value, so Rack's printf applies C's rules (negative width left-justifies, negative precision counts as none). `INT_MIN` as a width, which has no positive width in C, raises. Tested in Rack (`test_printf_through_racks_formatting`) |
| The crosswalk called the 17 records with no runtime meaning "bound" | A build macro or a declaration Rack never defines looked like a callable binding with no Python name | Shown as "no runtime meaning", with the reason on the linked page |
| `rack::system::runProcessDetached` on Windows | Its test called it on every platform; on Windows that asked UAC to elevate and blocked Rack's UI thread | Its test is its own and skipped on Windows, and the record cites only it, so Windows has no evidence for it: a manual check there, reported as an open gate, not a pass |

## What the passes found legitimate

- **Exclusions (111).** All are mechanical and owner-mandated: 69 carry Rack's `PRIVATE` attribute and 42 are
  pimpl `Internal` types that are incomplete in the `rack.hpp` closure. `bin/check_coverage.py check` rejects an
  exclusion the inventory classifies as public, one missing from `bindings/exclusions.json`, and one without a
  reason and reviewer.
- **Restrictions (210 records).** Every one either prevents what would crash or corrupt Rack (an assertion, an
  out-of-bounds index, a division by zero, a double free, a second owner, iterator invalidation), or applies
  ADR 003's callback rules (no Python on Rack's engine and driver threads; RackPy's native answer instead), or
  refuses what the guardrails forbid (`%n` writes through a pointer, `%p` prints an address, shell
  metacharacters reach `xdg-open`). None makes a public behaviour unreachable from Python without a stated
  reason, apart from the `string::f` finding above.
- **Templates (144).** Each maps every instantiation Rack's own code can express (a componentlibrary template, subscripted
  with its template arguments, gives that specialization; `isEven` and similar reach every result through `int64_t` and `uint64_t`),
  or a bounded instantiation contract the owner approved on 2026-10-26 and 2026-10-27 (coverage policy 10: the
  DSP ring buffers, resamplers, filters and polynomial evaluators), or Rack's own specializations where the
  primary template is deleted or declared only. Argument bounds that exist only to keep Rack's code defined
  (stack arrays, array indexes) are stated in each record.
- **Virtual declarations (437).** 434 records carry a callback strategy (ui 323, driver 42, lifecycle 33,
  ui-exclusive 19, engine 6, ui-shared 5, mixed 5, value 1); the other three are virtual destructors excluded as
  `PRIVATE`. Python overrides of engine-thread virtuals such as `Module::process` are refused in normal mode by
  ADR 003 and run as native kernels; the laboratory runs them offline.
- **Deprecated declarations (39).** All are bound (25 methods, 11 aliases, one class, one factory, one
  function) and each emits `DeprecationWarning`, with a behavioural test node per declaration
  (`bindings/deprecation-evidence.json`).
- **Macros (44).** 15 are constants, 13 functions, and 16 have no runtime meaning: build configuration
  (`GLEW_STATIC`, `NANOVG_GL2`, `GLEW_NO_GLU`, `SIMDE_ENABLE_NATIVE_ALIASES`, `_MM_FROUND_NO_EXC`), preprocessor
  token pasting and quoting (`CONCAT`, `TOSTRING` and their literal forms), linker-embedded data (`BINARY*`),
  code generators whose output has its own records (`DECLARE_VECTOR_OPERATOR_*`) and `PRIVATE`.
- **Platform branches.** The inventory is extracted for all four targets and verified natively in CI
  (`bin/inventory_sdk.py platform-check`). 17 conditional placeholders are absent or superseded on every
  platform; platform-only declarations (`fopen_u8`, `string.utf16_to_utf8` and `utf8_to_utf16` on Windows, the
  SIMDe macros on mac-arm64) are judged by inventory availability, and each platform's evidence is judged
  separately (`bin/check_coverage.py evidence`).
- **External-type adapters.** `json_t`, NanoVG and GLFW handles, `std::function` and C buffers are reached through
  RackPy's documented conversions and views; the release bar counts the semantic record, not the adapter
  (AGENTS.md: safe adapters do not count as completed mappings).

## For the independent reviewer (step 6)

Judgement calls a second reviewer should look at first: the 17 records with no runtime meaning; the bounded
template contracts (owner-approved, but still a limit on Rack's templates); ADR 003's refusals of Python
overrides on engine threads; and the Windows manual check of `runProcessDetached`.
