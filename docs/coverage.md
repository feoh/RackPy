# API coverage contract

## Establish a denominator before writing bindings

RP-02/RP-05 will create a normalized, versioned SDK inventory. Parse a translation
unit including only `rack.hpp`, using the actual SDK defines, dependency headers
and compiler configuration for each supported platform. Combine AST data with
preprocessor records; scrape neither Doxygen HTML nor regex alone.

Keep stable IDs from qualified C++ name, kind, canonical signature, template
parameters, source provenance and conditional availability. Enumerate inherited
public behavior without multiplying aliases into a misleading success count.
Record source revision, SDK checksums, tool versions and command lines.

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
