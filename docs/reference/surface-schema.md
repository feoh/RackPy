# SDK surface inventory schema

[`upstream/api-surface.json`](../../upstream/api-surface.json) is the
denominator for API coverage: every declaration and macro that a translation
unit containing only `#include <rack.hpp>` can see in Rack's own headers, plus
selected dependency constants needed at Rack's event and menu boundary. It is
an **inventory, not a coverage report**. Whether RackPy binds an entry is
recorded elsewhere (RP-11), keyed by the IDs defined here.

[`bin/inventory_sdk.py`](../../bin/inventory_sdk.py) generates it with libclang.
[Surface summary](surface-summary.md) gives the counts by family and kind.

```bash
bin/inventory_sdk.py extract          # regenerate upstream/api-surface.json
bin/inventory_sdk.py check            # re-extract; fail if the committed file differs
bin/inventory_sdk.py diff OLD NEW     # added, removed and changed IDs between two inventories
bin/inventory_sdk.py summary --write  # regenerate docs/reference/surface-summary.md
uv run pytest tests/inventory
```

The tool needs `clang++` and the `libclang.so` from the same LLVM release
(found with `clang++ -print-file-name=libclang.so`). It passes
`-resource-dir` from that compiler, so libclang and the builtin headers match.
The Python bindings are pinned to `clang==21.1.7`.

## How extraction works

1. Parse an in-memory `#include <rack.hpp>` translation unit with the SDK's
   Linux x64 flags (`-std=c++11 -march=nehalem -fPIC -funsafe-math-optimizations`)
   and a detailed preprocessing record. Function bodies are skipped.
2. **Any error diagnostic aborts the run and nothing is written.** So does an
   inventory with no declarations, or two distinct declarations that would
   share an ID.
3. Visit every cursor whose location is under the SDK's `include/` directory,
   following namespaces, `extern "C"` blocks, records, templates, friends and
   anonymous unions. Dependency headers (`dep/include`) and system headers are
   not generally inventoried; types they declare are recorded where Rack uses
   them. The selected GLFW input and Blendish corner constants are counted as
   `boundary_constant` entries when their headers are reached (policy 13).
4. Record every macro defined in a Rack header, and every range the
   preprocessor skipped, as a conditional block.
5. Repeat for each **variant** and merge by ID.

| Variant | What it is |
| --- | --- |
| `lin-x64` | The SDK's Linux x64 flags. |
| `lin-x64-simde` | The same without `-march=nehalem`. `__SSE4_2__` is then undefined and `simd/common.hpp` takes the SIMDe path that arm64 uses. This approximates the arm64 declaration surface; it is **not** an arm64 extraction. |
| `mac-arm64`, `mac-x64`, `win-x64` | Cross-target attempts. Without a macOS SDK or MinGW sysroot they fail at the first system header and are recorded as `not_extracted` with the reason. They become real extractions on native runners or with sysroots (RP-40, RP-41). |

Output is deterministic: entries sorted by ID, keys sorted, one entry per line.
Two runs with the same SDK and toolchain are byte-identical (tested).

## Top-level fields

| Field | Meaning |
| --- | --- |
| `schema_version` | `1` |
| `sdk` | Rack commit and the SDK archive and digest that were parsed |
| `toolchain` | libclang version string, bindings pin and the libclang library's SHA-256 |
| `extractions` | One record per variant: flags, `status` (`ok` or `not_extracted`), header and declaration counts, or the failure reason |
| `conditional_blocks` | Code a variant's preprocessor skipped: header, line range, the directive, a content hash, lexically detected `macro NAME` / `function NAME` declarations, and `skipped_in` variants |
| `boundary_types` | Dependency-header types (`header:Type`) used by public declarations, with use counts |
| `families` | Header to [coverage family](../scope.md#coverage-families) |
| `summary` | Counts by classification, kind and family |
| `entries` | The declarations |

## Entry IDs

An ID is `<kind>:<qualified name><template part>(<parameters>)<qualifiers>`.
Only the parts that apply are present. Examples:

```text
record:rack::engine::Module
method:rack::engine::Module::process(const rack::engine::Module::ProcessArgs &)
method:rack::widget::Widget::draw(NVGcontext *)
class_template:rack::dsp::TSchmittTrigger<type>
method:rack::dsp::TSchmittTrigger::process($c0, $c0, $c0)
function_template:rack::createParam<type>(rack::math::Vec, rack::engine::Module *, int)
function:rack::random::get<uint32_t>()
specialization:rack::simd::Vector<float, 4>
field:rack::engine::Port::voltages
enumerator:rack::dsp::BooleanTrigger::Event::TRIGGERED
macro:APP
boundary_constant:GLFW_KEY_A
boundary_constant:BND_CORNER_TOP
opaque_record:rack::patch::Manager
import:rack:using namespace math
import:rack::simd:using math::clamp
```

Rules that keep IDs stable:

- Names are fully qualified through **semantic** parents, so an out-of-line
  definition has the same ID as the in-class declaration.
- Parameter and return types are fully qualified **with typedef sugar kept**
  (`std::string`, `rack::simd::float_4`, `uint32_t`), not canonical types,
  which differ between standard libraries and platforms.
- Template parameters are positional: `$0`, `$1` for a function template's own
  parameters and `$c0`, `$c1` for an enclosing class template's. Renaming
  `T` upstream does not change any ID. The `<type, value>` part lists parameter
  kinds; `...` marks a pack.
- Members of a class template are named without arguments
  (`rack::dsp::RingBuffer::push`). Explicit and partial specializations keep
  their arguments (`rack::simd::Vector<float, 4>::...`).
- Explicit specializations of function templates carry their arguments, spelled
  from the matching written return or parameter type
  (`random::get<uint32_t>()`, not `get<unsigned int>()`).
- `const` methods end in ` const`. Variadic functions end in `...`.
- Moving a declaration to another header changes `header`, not the ID.
- USRs are not used as IDs: they encode canonical library types
  (`std::__cxx11::basic_string`) and differ between libstdc++ and libc++.

## Entry fields

Every entry has `id`, `kind`, `name` (qualified), `header`, `line` and
`classification`. Other fields appear only when they apply.

| Field | Applies to | Meaning |
| --- | --- | --- |
| `access` | declarations | `public`, `protected`, `private`, or `namespace` |
| `params` | functions | `name`, `type` and, when present, `default` (source tokens) |
| `returns` | functions except constructors and destructors | Return type |
| `template_params` | templates | `name`, `kind` (`type`, `value`, `template`), `default`, `type` for value parameters, `pack` |
| `specialization_args` | function specializations | Template arguments |
| `virtual`, `pure`, `override`, `final`, `static`, `explicit`, `deleted`, `defaulted`, `inline`, `noexcept`, `variadic`, `friend`, `extern_c` | functions | Declaration flags |
| `type` | fields and variables | Declared type |
| `value` | variables, enumerators | Initializer tokens, or the enumerator's integer value |
| `visible_as` | enumerators of unscoped enums | Name in the enclosing scope |
| `bases` | records | `type`, `access`, `virtual` |
| `abstract` | records | Has a pure virtual method |
| `tag` | records | `struct`, `class` or `union` |
| `anonymous_group` | members of anonymous unions and structs | Members with the same value alias the same storage |
| `scoped`, `underlying` | enums | `enum class`, and the underlying type |
| `target` | aliases, imports | Aliased type, or the `using` text |
| `body`, `params` | macros | Replacement tokens, and parameter names for function-like macros |
| `types` | declarations | Types referenced in the signature, as `header:Type` for dependency headers, `std:Name` or `system:Name` |
| `conditions` | anything inside `#if` | The enclosing directive stack, such as `!(defined ARCH_MAC)` |
| `absent_in` | entries missing from some variants | Variant names |
| `private` | PRIVATE declarations | From the attribute's message, found through libclang availability, not tokens |
| `deprecated` | `DEPRECATED` declarations | `true`, or the attribute message |
| `comment_deprecated` | documentation says deprecated | Matches `DEPRECATED` or `Deprecated` in the doc comment |
| `deprecated_scope` | members of a namespace documented as deprecated | The namespace, such as `rack::event` |
| `unstable` | documentation says "Unstable API" | |
| `pimpl` | `Internal* internal` members | The pointee is an incomplete `Internal` struct |
| `evidence` | non-`public` entries | Why the classification was made |

### Kinds

`record`, `specialization`, `class_template`, `partial_specialization`,
`opaque_record` (declared, never defined in the closure), `field`,
`static_field`, `variable`, `function`, `method`, `constructor`, `destructor`,
`conversion`, `function_template`, `method_template`, `enum`, `enumerator`,
`alias`, `alias_template`, `object_macro`, `function_macro` and `import`.

### Classifications

| Classification | Rule | Counts toward the denominator? |
| --- | --- | --- |
| `public` | Reachable from rack.hpp, no exclusion applies | Yes |
| `public_opaque` | Declared but never defined in the closure (`patch::Manager`, `midiloopback::Context`, the primary `simd::Vector`) | Yes, mapped as opaque handles ([policy 4](../coverage.md#completeness-policy)) |
| `excluded_internal` | `PRIVATE` attribute, or a pimpl `Internal` type or member | No, with evidence |
| `inaccessible` | C++ `private` access | No, with evidence |
| `import` | `using` declarations and directives | No; they define expected name paths only |

Anything that cannot be classified is a bug in the tool, not a reason to drop
the entry: extraction stops instead.

## What it does not do yet

- macOS and Windows have not been extracted: there are no sysroots on the
  planning machine. Their Rack-only declarations are visible as conditional
  blocks (for example `string::UTF16toUTF8` on Windows).
- Inherited members are not copied into derived records. Consumers walk
  `bases`, so nothing is double-counted.
- Conditional blocks are scanned lexically, because the preprocessor removed
  them. Native extraction on that platform supersedes the lexical names.
